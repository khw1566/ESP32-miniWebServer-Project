/* Private tool bridge; included after the admin/settings helpers. */
#define AGENT_MAX_FILE (24 * 1024)
#define AGENT_MAX_REQUEST (48 * 1024)
#define AGENT_STAGE STORAGE_BASE_PATH "/.agent.tmp"

static esp_err_t agent_json(httpd_req_t *req, const char *status, cJSON *value) {
    if (!value) return httpd_resp_send_500(req);
    char *json = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (!json) return httpd_resp_send_500(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, json);
    free(json);
    return result;
}

static esp_err_t agent_error(httpd_req_t *req, const char *status, const char *message) {
    cJSON *value = cJSON_CreateObject();
    if (value) {
        cJSON_AddBoolToObject(value, "ok", false);
        cJSON_AddStringToObject(value, "error", message);
    }
    return agent_json(req, status, value);
}

static const char *agent_string(cJSON *args, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(args, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static bool agent_filename(const char *name) {
    if (!name || !*name || strlen(name) > 24 || name[0] == '.' || strstr(name, "..")) return false;
    for (const char *p = name; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) return false;
    }
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm") ||
                   !strcasecmp(ext, ".css") || !strcasecmp(ext, ".js") ||
                   !strcasecmp(ext, ".json") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".md"));
}

static uint32_t agent_hash(const unsigned char *data, size_t size) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < size; i++) hash = (hash ^ data[i]) * 16777619U;
    return hash;
}

/* Hash/edit validation and JSON responses never keep a whole file in RAM. */
static bool agent_file_version(const char *path, size_t expected, char version[16]) {
    FILE *file=fopen(path,"rb");if(!file) return false;
    unsigned char chunk[512];size_t n,total=0;uint32_t hash=2166136261U;bool valid=true;
    while((n=fread(chunk,1,sizeof(chunk),file))>0) {
        if(memchr(chunk,0,n)) {valid=false;break;}
        for(size_t i=0;i<n;++i) hash=(hash^chunk[i])*16777619U;
        total+=n;
    }
    if(ferror(file)||total!=expected) valid=false;
    fclose(file);
    if(valid) snprintf(version,16,"%08lx",(unsigned long)hash);
    return valid;
}
static size_t agent_json_escape(unsigned char ch, char out[6]) {
    if(ch=='"'||ch=='\\') {out[0]='\\';out[1]=ch;return 2;}
    if(ch<32) {
        const char *hex="0123456789abcdef";
        out[0]='\\';out[1]='u';out[2]='0';out[3]='0';
        out[4]=hex[ch>>4];out[5]=hex[ch&15];return 6;
    }
    out[0]=ch;return 1;
}
static esp_err_t agent_read_file_stream(httpd_req_t *req,const char *path,const char *name,const char *version,size_t expected) {
    FILE *file=fopen(path,"rb");
    if(!file) return agent_error(req,"500 Internal Server Error","파일을 읽지 못했습니다.");
    char out[1024];unsigned char chunk[512];size_t used=0,total=0,n;
    snprintf(out,sizeof(out),"{\"ok\":true,\"name\":\"%s\",\"version\":\"%s\",\"content\":\"",name,version);
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr_chunk(req,out);
    while(result==ESP_OK && (n=fread(chunk,1,sizeof(chunk),file))>0) {
        total+=n;
        for(size_t i=0;i<n;++i) {
            unsigned char ch=chunk[i];
            if(used+6>sizeof(out)) {result=httpd_resp_send_chunk(req,out,used);used=0;if(result!=ESP_OK) break;}
            char escaped[6];size_t length=agent_json_escape(ch,escaped);
            memcpy(out+used,escaped,length);used+=length;
        }
    }
    if(result==ESP_OK && (ferror(file)||total!=expected)) result=ESP_FAIL;
    fclose(file);
    if(result==ESP_OK && used) result=httpd_resp_send_chunk(req,out,used);
    if(result==ESP_OK) result=httpd_resp_sendstr_chunk(req,"\"}");
    if(result==ESP_OK) result=httpd_resp_send_chunk(req,NULL,0);
    return result;
}

static void agent_recover_files(void) {
    DIR *dir = opendir(STORAGE_BASE_PATH);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        size_t n = strlen(ent->d_name);
        if (n <= 6 || strcmp(ent->d_name + n - 6, ".aibak") != 0) continue;
        char name[32] = {0};
        if (n - 6 > 24) continue;
        memcpy(name, ent->d_name, n - 6);
        if (!agent_filename(name)) continue;
        char original[64], backup[64];
        snprintf(original, sizeof(original), "%s/%s", STORAGE_BASE_PATH, name);
        snprintf(backup, sizeof(backup), "%s/%s.aibak", STORAGE_BASE_PATH, name);
        struct stat st;
        if (stat(original, &st) == 0) unlink(backup);
        else if (rename(backup, original) != 0) ESP_LOGE(TAG, "Agent backup recovery failed: %s", name);
    }
    closedir(dir);
    unlink(AGENT_STAGE);
}

#include "web_agent_diagnostics.inc"

static esp_err_t agent_execute(httpd_req_t *req, const char *tool, cJSON *args) {
    if (!strcmp(tool, "get_diagnostics")) return agent_diagnostics(req);
    if (!strcmp(tool, "get_logs")) return agent_logs(req);
    if (!strcmp(tool, "inspect_media")) return agent_media_info(req, args);
    if (!strcmp(tool, "get_settings")) {
        char page[128] = {0};
        get_initial_page(page, sizeof(page));
        cJSON *out = cJSON_CreateObject();
        if (!out) return httpd_resp_send_500(req);
        cJSON_AddBoolToObject(out, "ok", true);
        cJSON_AddStringToObject(out, "main_page", page);
        cJSON_AddNumberToObject(out, "builtin_view", get_initial_view_mode());
        cJSON_AddNumberToObject(out, "max_file_bytes", AGENT_MAX_FILE);
        cJSON_AddStringToObject(out, "note", "Empty main_page falls back to index.html, then builtin_view. AP root remains Wi-Fi setup.");
        return agent_json(req, "200 OK", out);
    }
    if (!strcmp(tool, "list_files")) {
        DIR *dir = opendir(STORAGE_BASE_PATH);
        if (!dir) return agent_error(req, "503 Service Unavailable", "저장소를 사용할 수 없습니다.");
        httpd_resp_set_type(req,"application/json; charset=utf-8");
        httpd_resp_set_hdr(req,"Cache-Control","no-store");
        esp_err_t result=httpd_resp_sendstr_chunk(req,"{\"ok\":true,\"files\":[");
        struct dirent *ent;
        int count = 0;bool truncated=false;
        while (result==ESP_OK && (ent = readdir(dir))) {
            if (ent->d_name[0] == '.' || strstr(ent->d_name, ".aibak")) continue;
            if (count >= 128) {truncated=true;break;}
            char path[280]; struct stat st;
            snprintf(path, sizeof(path), "%s/%s", STORAGE_BASE_PATH, ent->d_name);
            if (stat(path, &st) != 0) continue;
            cJSON *file = cJSON_CreateObject();
            if (!file) {result=ESP_ERR_NO_MEM;break;}
            cJSON_AddStringToObject(file, "name", ent->d_name);
            cJSON_AddNumberToObject(file, "bytes", st.st_size);
            int64_t modified = file_date_read(ent->d_name);
            if (modified) cJSON_AddNumberToObject(file, "modified_at", (double)modified);
            else cJSON_AddNullToObject(file, "modified_at");
            cJSON_AddBoolToObject(file, "editable", agent_filename(ent->d_name) && st.st_size <= AGENT_MAX_FILE);
            char *json=cJSON_PrintUnformatted(file);cJSON_Delete(file);
            if(!json) {result=ESP_ERR_NO_MEM;break;}
            if(count) result=httpd_resp_sendstr_chunk(req,",");
            if(result==ESP_OK) result=httpd_resp_sendstr_chunk(req,json);
            free(json);++count;
        }
        closedir(dir);
        if(result==ESP_OK) result=httpd_resp_sendstr_chunk(req,truncated?"],\"truncated\":true}":"]}");
        if(result==ESP_OK) result=httpd_resp_send_chunk(req,NULL,0);
        return result;
    }
    bool reading = !strcmp(tool, "read_file"), writing = !strcmp(tool, "write_file");
    bool setting_home = !strcmp(tool, "set_home");
    if (!reading && !writing && !setting_home) return agent_error(req, "400 Bad Request", "지원하지 않는 도구입니다.");
    const char *name = agent_string(args, "name");
    if (!agent_filename(name)) return agent_error(req, "400 Bad Request", "파일명은 영문·숫자·점·밑줄·하이픈으로 최대 24자까지 입력하세요. 폴더 경로는 사용할 수 없습니다.");
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_BASE_PATH, name);
    if (setting_home) {
        char page[32]; snprintf(page, sizeof(page), "/%s", name);
        if (!is_valid_initial_page(page)) return agent_error(req, "400 Bad Request", "메인 화면은 보드에 저장된 HTML 파일로 지정해야 합니다.");
        if (set_initial_page(page) != ESP_OK) return agent_error(req, "500 Internal Server Error", "메인 화면 설정을 저장하지 못했습니다.");
        cJSON *out = cJSON_CreateObject();
        if (!out) return httpd_resp_send_500(req);
        cJSON_AddBoolToObject(out, "ok", true);
        cJSON_AddStringToObject(out, "main_page", page);
        return agent_json(req, "200 OK", out);
    }
    struct stat st;
    bool exists = stat(path, &st) == 0;
    if (reading && !exists) return agent_error(req, "404 Not Found", "파일을 찾을 수 없습니다.");
    char version[16] = "new";
    if (exists) {
        if (st.st_size < 0 || st.st_size > AGENT_MAX_FILE) return agent_error(req, "413 Payload Too Large", "파일이 도구의 최대 크기인 24 KiB를 초과했습니다.");
        if(!agent_file_version(path,(size_t)st.st_size,version))
            return agent_error(req,"400 Bad Request","텍스트로 읽을 수 없는 파일입니다.");
    }
    if (reading) return agent_read_file_stream(req,path,name,version,(size_t)st.st_size);
    const char *expected = agent_string(args, "expected_version"), *content = agent_string(args, "content");
    if (!expected || strcmp(expected, version)) return agent_error(req, "409 Conflict", "파일이 변경되었거나 이미 존재합니다. 파일을 다시 읽은 뒤 수정하세요. 새 파일 표시는 실제로 없는 파일에만 사용할 수 있습니다.");
    if (!content || strlen(content) > AGENT_MAX_FILE) return agent_error(req, "413 Payload Too Large", "파일 내용은 24 KiB 이하의 텍스트여야 합니다.");
    size_t length = strlen(content);
    FILE *file = fopen(AGENT_STAGE, "wb");
    if (!file) return agent_error(req, "507 Insufficient Storage", "저장 준비용 임시 파일을 만들지 못했습니다.");
    bool failed = fwrite(content, 1, length, file) != length;
    if (fclose(file) != 0) failed = true;
    if (failed) { unlink(AGENT_STAGE); return agent_error(req, "507 Insufficient Storage", "저장에 실패했습니다. 기존 파일은 유지됩니다."); }
    // Include space for the entire source path plus the backup suffix.
    char backup[sizeof(path) + sizeof(".aibak") - 1];
    snprintf(backup, sizeof(backup), "%s.aibak", path);
    if (exists && rename(path, backup) != 0) { unlink(AGENT_STAGE); return agent_error(req, "500 Internal Server Error", "기존 파일을 백업하지 못했습니다."); }
    if (rename(AGENT_STAGE, path) != 0) {
        if (exists && rename(backup, path) != 0) ESP_LOGE(TAG, "Agent rollback pending: %s", name);
        unlink(AGENT_STAGE);
        return agent_error(req, "500 Internal Server Error", "파일 적용에 실패했습니다. 기존 파일은 유지되거나 재부팅 복구용 백업에 남아 있습니다.");
    }
    if (exists) unlink(backup);
    cJSON *client_date = cJSON_GetObjectItemCaseSensitive(args, "modified_at");
    int64_t modified = cJSON_IsNumber(client_date) ? file_date_valid(client_date->valuedouble) : 0;
    file_date_write(name, modified ? modified : file_date_now());
    cJSON *out = cJSON_CreateObject();
    if (!out) return httpd_resp_send_500(req);
    snprintf(version, sizeof(version), "%08lx", (unsigned long)agent_hash((const unsigned char *)content, length));
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "name", name);
    cJSON_AddStringToObject(out, "version", version);
    cJSON_AddNumberToObject(out, "bytes", length);
    return agent_json(req, "200 OK", out);
}

static esp_err_t api_agent_post_handler(httpd_req_t *req) {
    if (!ai_require_auth(req)) return ESP_OK;
    if (req->content_len <= 0 || req->content_len > AGENT_MAX_REQUEST) return agent_error(req, "413 Payload Too Large", "도구 요청이 최대 크기인 48 KiB를 초과했습니다.");
    if(heap_caps_get_free_size(MALLOC_CAP_8BIT)<(size_t)req->content_len*2+24*1024)
        return agent_error(req,"503 Service Unavailable","메모리가 부족합니다. 요청을 줄이거나 잠시 후 다시 시도하세요.");
    char *body = malloc(req->content_len + 1);
    if (!body) return httpd_resp_send_500(req);
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) { free(body); return agent_error(req, "408 Request Timeout", "도구 요청을 끝까지 받지 못했습니다."); }
        received += n;
    }
    body[received] = 0;
    // Bound parser recursion before cJSON touches the HTTP task stack.
    int depth = 0;size_t nodes=1;
    bool quoted = false, escaped = false, valid = memchr(body, 0, received) == NULL;
    for (size_t i = 0; valid && i < received; i++) {
        char ch = body[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') {
                escaped = true;
                if (i + 5 < received && !strncmp(body + i, "\\u0000", 6)) valid = false;
            } else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') {++nodes; if (++depth > 8) valid = false; }
        else if (ch == '}' || ch == ']') { if (--depth < 0) valid = false; }
        else if (ch == ':' || ch == ',') ++nodes;
        if(nodes>384) valid=false;
    }
    if(heap_caps_get_free_size(MALLOC_CAP_8BIT)<received+nodes*(sizeof(cJSON)+32)+16*1024) {
        free(body);return agent_error(req,"503 Service Unavailable","메모리가 부족합니다. 요청을 줄여 다시 시도하세요.");
    }
    cJSON *request = valid ? cJSON_ParseWithLengthOpts(body, received + 1, NULL, true) : NULL;
    free(body);
    if (!request) return agent_error(req, "400 Bad Request", "요청 데이터 형식이 올바르지 않습니다.");
    const char *tool = agent_string(request, "name");
    cJSON *args = cJSON_GetObjectItemCaseSensitive(request, "arguments");
    esp_err_t result;
    if (!tool || !cJSON_IsObject(args)) result = agent_error(req, "400 Bad Request", "도구 이름과 실행 인자를 확인하세요.");
    else result = agent_execute(req, tool, args);
    cJSON_Delete(request);
    return result;
}
