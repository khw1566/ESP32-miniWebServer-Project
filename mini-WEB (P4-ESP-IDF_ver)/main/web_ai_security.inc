/* Private AI authentication and password-wrapped credential storage. */
#define AI_SECURITY_VERSION 1
#define AI_KDF_ROUNDS 20000
/* Login persists in an HttpOnly cookie: renewed on each use for 12 hours of
 * inactivity, at most 7 days after login. Sessions live in RAM only, so a
 * reboot or a password change signs every browser out. */
#define AI_SESSION_US (12LL * 60 * 60 * 1000000)
#define AI_SESSION_MAX_US (7LL * 24 * 60 * 60 * 1000000)
#define AUTH_COOKIE "mw_session"
typedef struct {
    uint32_t version;
    unsigned char password_salt[16], password_hash[32], key_salt[16];
    unsigned char iv[12], tag[16];
    uint32_t key_len;
    unsigned char ciphertext[256];
} ai_security_record_t;
typedef struct {
    char token[65];
    unsigned char wrapping_key[32];
    int64_t created, expires;
} ai_session_t;
static ai_session_t s_ai_sessions[4];
static ai_session_t *ai_session_for(httpd_req_t *req);
static unsigned s_ai_login_failures;
static int64_t s_ai_login_blocked_until;

static uint32_t s_auth_failed_total;
static bool s_auth_audit_loaded, s_auth_audit_available = true, s_auth_audit_saved = true;
static void auth_audit_load(void) {
    if (s_auth_audit_loaded) return;
    s_auth_audit_loaded = true;
    nvs_handle_t h;
    esp_err_t err = nvs_open("auth_audit", NVS_READONLY, &h);
    if (err == ESP_OK) { err = nvs_get_u32(h, "failed_total", &s_auth_failed_total); nvs_close(h); }
    s_auth_audit_available = s_auth_audit_saved = err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}
static void auth_audit_failed(void) {
    auth_audit_load();
    if (s_auth_failed_total < UINT32_MAX) ++s_auth_failed_total;
    if (!s_auth_audit_available) { s_auth_audit_saved = false; return; }
    nvs_handle_t h;
    esp_err_t err = nvs_open("auth_audit", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, "failed_total", s_auth_failed_total);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    s_auth_audit_saved = err == ESP_OK;
}
static void auth_status_add(cJSON *value) {
    auth_audit_load();
    int64_t remaining = s_ai_login_blocked_until - esp_timer_get_time();
    unsigned retry = remaining > 0 ? (unsigned)((remaining + 999999) / 1000000) : 0;
    unsigned failed = retry ? 5 : s_ai_login_failures;
    cJSON_AddNumberToObject(value, "failed_attempts", failed);
    cJSON_AddNumberToObject(value, "failed_total", s_auth_failed_total);
    cJSON_AddNumberToObject(value, "remaining_attempts", 5 - failed);
    cJSON_AddNumberToObject(value, "retry_after", retry);
    cJSON_AddBoolToObject(value, "record_saved", s_auth_audit_saved);
}
static esp_err_t api_auth_status_handler(httpd_req_t *req) {
    cJSON *value = cJSON_CreateObject();
    if (!value) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(value, "ok", true); auth_status_add(value);
    cJSON_AddBoolToObject(value, "authenticated", ai_session_for(req) != NULL);
    char *body = cJSON_PrintUnformatted(value); cJSON_Delete(value);
    if (!body) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, body); free(body); return err;
}

static void ai_wipe(void *memory, size_t size) { mbedtls_platform_zeroize(memory, size); }
static bool ai_equal(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    unsigned char diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= x[i] ^ y[i];
    return diff == 0;
}
static bool ai_derive(const char *password, const unsigned char salt[16], unsigned char out[32]) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        (const unsigned char *)password, strlen(password), salt, 16, AI_KDF_ROUNDS, 32, out) == 0;
}
static esp_err_t ai_record_read(ai_security_record_t *record) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t length = sizeof(*record);
    err = nvs_get_blob(h, "security_v1", record, &length);
    nvs_close(h);
    if (err == ESP_OK && (length != sizeof(*record) || record->version != AI_SECURITY_VERSION ||
                         record->key_len >= sizeof(record->ciphertext))) return ESP_FAIL;
    return err;
}
static esp_err_t ai_record_write(const ai_security_record_t *record) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "security_v1", record, sizeof(*record));
    if (err == ESP_OK) {
        esp_err_t erased = nvs_erase_key(h, "admin_pw");
        if (erased != ESP_OK && erased != ESP_ERR_NVS_NOT_FOUND) err = erased;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
static bool check_admin_password(const char *password) {
    if (!password || !*password || strlen(password) >= 64) return false;
    ai_security_record_t record = {0};
    esp_err_t err = ai_record_read(&record);
    if (err == ESP_OK) {
        unsigned char hash[32];
        bool valid = ai_derive(password, record.password_salt, hash) && ai_equal(hash, record.password_hash, 32);
        /* Upgrade only the old factory default; preserve user passwords and keys. */
        if (!valid && strcmp(password, DEFAULT_ADMIN_PW) == 0 && record.key_len == 0 &&
            ai_derive("0000", record.password_salt, hash) && ai_equal(hash, record.password_hash, 32)) {
            esp_fill_random(record.password_salt, sizeof(record.password_salt));
            esp_fill_random(record.key_salt, sizeof(record.key_salt));
            valid = ai_derive(DEFAULT_ADMIN_PW, record.password_salt, record.password_hash) && ai_record_write(&record) == ESP_OK;
        }
        ai_wipe(hash, sizeof(hash)); ai_wipe(&record, sizeof(record));
        return valid;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) return false;
    /* Migrate legacy plaintext only after a successful password check. */
    char legacy[64] = DEFAULT_ADMIN_PW;
    nvs_handle_t h;
    err = nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t length = sizeof(legacy);
        err = nvs_get_str(h, "admin_pw", legacy, &length);
        nvs_close(h);
    }
    if (strcmp(legacy, "0000") == 0) snprintf(legacy, sizeof(legacy), "%s", DEFAULT_ADMIN_PW);
    bool valid = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) &&
                 strlen(password) == strlen(legacy) && ai_equal(password, legacy, strlen(legacy));
    ai_wipe(legacy, sizeof(legacy));
    if (!valid) return false;
    record.version = AI_SECURITY_VERSION;
    esp_fill_random(record.password_salt, sizeof(record.password_salt));
    esp_fill_random(record.key_salt, sizeof(record.key_salt));
    valid = ai_derive(password, record.password_salt, record.password_hash) && ai_record_write(&record) == ESP_OK;
    ai_wipe(&record, sizeof(record));
    return valid;
}
static bool verify_admin_password(const char *password) {
    int64_t now = esp_timer_get_time();
    if (now < s_ai_login_blocked_until) return false;
    bool valid = check_admin_password(password);
    if (valid) s_ai_login_failures = 0;
    else {
        auth_audit_failed();
        if (++s_ai_login_failures >= 5) { s_ai_login_failures = 0; s_ai_login_blocked_until = esp_timer_get_time() + 60LL * 1000000; }
    }
    return valid;
}
static bool ai_key_decrypt(const ai_security_record_t *record, const unsigned char key[32], char out[256]) {
    if (!record->key_len || record->key_len >= 256) return false;
    mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
    int err = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!err) err = mbedtls_gcm_auth_decrypt(&ctx, record->key_len, record->iv, sizeof(record->iv),
        (const unsigned char *)"mini-ai-key-v1", 14, record->tag, sizeof(record->tag), record->ciphertext, (unsigned char *)out);
    mbedtls_gcm_free(&ctx);
    if (err) { ai_wipe(out, 256); return false; }
    out[record->key_len] = 0;
    return true;
}
static bool ai_key_encrypt(ai_security_record_t *record, const unsigned char key[32], const char *value) {
    size_t length = strlen(value);
    if (!length || length >= sizeof(record->ciphertext)) return false;
    esp_fill_random(record->iv, sizeof(record->iv));
    ai_wipe(record->ciphertext, sizeof(record->ciphertext));
    mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
    int err = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!err) err = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, length, record->iv, sizeof(record->iv),
        (const unsigned char *)"mini-ai-key-v1", 14, (const unsigned char *)value, record->ciphertext, sizeof(record->tag), record->tag);
    mbedtls_gcm_free(&ctx);
    if (!err) record->key_len = length;
    return err == 0;
}
static void ai_sessions_clear(void) { ai_wipe(s_ai_sessions, sizeof(s_ai_sessions)); }
static esp_err_t set_admin_password(const char *new_password, const char *old_password) {
    if (!verify_admin_password(old_password)) return ESP_ERR_INVALID_ARG;
    ai_security_record_t record = {0};
    if (ai_record_read(&record) != ESP_OK) return ESP_FAIL;
    char plaintext[256] = {0}; unsigned char old_key[32] = {0}, new_key[32] = {0};
    bool ok = true;
    if (record.key_len) ok = ai_derive(old_password, record.key_salt, old_key) && ai_key_decrypt(&record, old_key, plaintext);
    esp_fill_random(record.password_salt, sizeof(record.password_salt));
    esp_fill_random(record.key_salt, sizeof(record.key_salt));
    ok = ok && ai_derive(new_password, record.password_salt, record.password_hash);
    if (record.key_len) ok = ok && ai_derive(new_password, record.key_salt, new_key) && ai_key_encrypt(&record, new_key, plaintext);
    esp_err_t err = ok ? ai_record_write(&record) : ESP_FAIL;
    ai_wipe(plaintext, sizeof(plaintext)); ai_wipe(old_key, sizeof(old_key)); ai_wipe(new_key, sizeof(new_key)); ai_wipe(&record, sizeof(record));
    if (err == ESP_OK) ai_sessions_clear();
    return err;
}
static esp_err_t ai_error(httpd_req_t *req, const char *status, const char *message) {
    cJSON *value = cJSON_CreateObject();
    if (!value) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(value, "ok", false); cJSON_AddStringToObject(value, "error", message); auth_status_add(value);
    char *body = cJSON_PrintUnformatted(value); cJSON_Delete(value);
    if (!body) return httpd_resp_send_500(req);
    httpd_resp_set_status(req, status); httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store"); httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t err = httpd_resp_sendstr(req, body); free(body); return err;
}
#include "web_ai_request.inc"
/* A Bearer header (AI studio) or the session cookie (any page of this board). */
static ai_session_t *ai_session_for(httpd_req_t *req) {
    char header[80] = {0}, token[72] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK &&
        strlen(header) == 71 && !strncmp(header, "Bearer ", 7)) memcpy(token, header + 7, 64);
    else {
        size_t length = sizeof(token);
        if (httpd_req_get_cookie_val(req, AUTH_COOKIE, token, &length) != ESP_OK || strlen(token) != 64) {
            ai_wipe(token, sizeof(token)); return NULL;
        }
    }
    ai_wipe(header, sizeof(header));
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < 4; ++i) {
        if (s_ai_sessions[i].expires <= now) { ai_wipe(&s_ai_sessions[i], sizeof(s_ai_sessions[i])); continue; }
        if (ai_equal(token, s_ai_sessions[i].token, 64)) {
            int64_t renewed = now + AI_SESSION_US, limit = s_ai_sessions[i].created + AI_SESSION_MAX_US;
            s_ai_sessions[i].expires = renewed < limit ? renewed : limit;
            ai_wipe(token, sizeof(token));
            return &s_ai_sessions[i];
        }
    }
    ai_wipe(token, sizeof(token));
    return NULL;
}
/* Session for a verified password. When all four are in use, the least
 * recently used one is replaced instead of refusing the login. */
static ai_session_t *ai_session_create(const char *password) {
    ai_security_record_t record = {0}; unsigned char wrapping[32] = {0};
    bool ok = ai_record_read(&record) == ESP_OK && ai_derive(password, record.key_salt, wrapping);
    ai_wipe(&record, sizeof(record));
    if (!ok) { ai_wipe(wrapping, sizeof(wrapping)); return NULL; }
    int64_t now = esp_timer_get_time();
    ai_session_t *session = &s_ai_sessions[0];
    for (unsigned i = 0; i < 4; ++i) {
        if (s_ai_sessions[i].expires <= now) { session = &s_ai_sessions[i]; break; }
        if (s_ai_sessions[i].expires < session->expires) session = &s_ai_sessions[i];
    }
    ai_wipe(session, sizeof(*session)); unsigned char random[32]; esp_fill_random(random, sizeof(random));
    for (unsigned i = 0; i < 32; ++i) snprintf(session->token + i * 2, 3, "%02x", random[i]);
    memcpy(session->wrapping_key, wrapping, 32); ai_wipe(wrapping, sizeof(wrapping)); ai_wipe(random, sizeof(random));
    session->created = now; session->expires = now + AI_SESSION_US;
    return session;
}
/* httpd keeps the pointer: the buffer must live until the response is sent. */
static void auth_cookie_header(char out[128], const char *token) {
    if (token) snprintf(out, 128, AUTH_COOKIE "=%.64s; Path=/; Max-Age=604800; HttpOnly; SameSite=Strict", token);
    else snprintf(out, 128, AUTH_COOKIE "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
}
static bool ai_require_auth(httpd_req_t *req) {
    if (ai_session_for(req)) return true;
    ai_error(req, "401 Unauthorized", "AI 로그인이 필요하거나 세션이 만료되었습니다.");
    return false;
}
static esp_err_t ai_login_post_handler(httpd_req_t *req) {
    int64_t now = esp_timer_get_time();
    if (now < s_ai_login_blocked_until) return ai_error(req, "429 Too Many Requests", "로그인 시도 횟수를 초과했습니다. 표시된 대기 시간이 지난 후 다시 입력하세요.");
    cJSON *body = ai_read_json(req, 512);
    cJSON *password = body ? cJSON_GetObjectItemCaseSensitive(body, "password") : NULL;
    if (!cJSON_IsString(password)) { cJSON_Delete(body); return ai_error(req, "400 Bad Request", "비밀번호를 입력하세요."); }
    bool valid = verify_admin_password(password->valuestring);
    if (!valid) {
        ai_wipe(password->valuestring, strlen(password->valuestring)); cJSON_Delete(body);
        return ai_error(req, "403 Forbidden", "관리자 비밀번호가 일치하지 않습니다.");
    }
    if (strlen(password->valuestring) < 8) {
        ai_wipe(password->valuestring, strlen(password->valuestring)); cJSON_Delete(body);
        return ai_error(req, "403 Forbidden", "AI를 사용하려면 관리자 설정에서 비밀번호를 8자리 이상으로 변경하세요.");
    }
    ai_session_t *session = ai_session_create(password->valuestring);
    ai_wipe(password->valuestring, strlen(password->valuestring)); cJSON_Delete(body);
    if (!session) return httpd_resp_send_500(req);
    s_ai_login_failures = 0;
    char response[160], cookie[128];
    snprintf(response, sizeof(response), "{\"ok\":true,\"token\":\"%s\",\"expires_in\":%d}", session->token, (int)(AI_SESSION_US / 1000000));
    auth_cookie_header(cookie, session->token);
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return httpd_resp_sendstr(req, response);
}
static esp_err_t ai_logout_post_handler(httpd_req_t *req) {
    ai_session_t *session = ai_session_for(req);
    if (session) ai_wipe(session, sizeof(*session));
    char cookie[128]; auth_cookie_header(cookie, NULL);
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
static esp_err_t ai_key_get_handler(httpd_req_t *req) {
    if (!ai_require_auth(req)) return ESP_OK;
    ai_security_record_t record = {0};
    if (ai_record_read(&record) != ESP_OK) return httpd_resp_send_500(req);
    bool configured = record.key_len != 0; ai_wipe(&record, sizeof(record));
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, configured ? "{\"ok\":true,\"configured\":true}" : "{\"ok\":true,\"configured\":false}");
}
static esp_err_t ai_key_post_handler(httpd_req_t *req) {
    ai_session_t *session = ai_session_for(req);
    if (!session) return ai_error(req, "401 Unauthorized", "AI 로그인이 필요합니다.");
    cJSON *body = ai_read_json(req, 768);
    cJSON *value = body ? cJSON_GetObjectItemCaseSensitive(body, "key") : NULL;
    cJSON *remove = body ? cJSON_GetObjectItemCaseSensitive(body, "remove") : NULL;
    bool deleting = cJSON_IsTrue(remove);
    if (cJSON_IsString(value)) {
        char *key=value->valuestring;size_t length=strlen(key),original_length=length,start=0;
        while(start<length && isspace((unsigned char)key[start])) ++start;
        while(length>start && isspace((unsigned char)key[length-1])) --length;
        memmove(key,key+start,length-start);key[length-start]=0;
        length=strlen(key);
        if(length>=2 && (key[0]=='\"' || key[0]=='\'' || key[0]=='`') && key[length-1]==key[0]) {
            memmove(key,key+1,length-2);key[length-2]=0;
            length=strlen(key);start=0;
            while(start<length && isspace((unsigned char)key[start])) ++start;
            while(length>start && isspace((unsigned char)key[length-1])) --length;
            memmove(key,key+start,length-start);key[length-start]=0;
        }
        size_t normalized_length=strlen(key);
        ai_wipe(key+normalized_length+1,original_length-normalized_length);
    }
    bool valid = deleting || (cJSON_IsString(value) && strlen(value->valuestring) >= 16 && strlen(value->valuestring) < 256);
    if (valid && !deleting) for (const char *p = value->valuestring; *p; ++p)
        if (!((*p>='A' && *p<='Z') || (*p>='a' && *p<='z') || (*p>='0' && *p<='9') || strchr("-_.~+/=",*p))) valid = false;
    ai_security_record_t record = {0};
    const char *failure = "키 형식 오류: 키 원문 16~255자를 입력하세요. 키 내부 공백이나 URL·명령문은 지원하지 않습니다.";
    esp_err_t storage_error = ESP_OK;
    bool ok = valid;
    if (ok) {
        storage_error = ai_record_read(&record);
        ok = storage_error == ESP_OK;
        if (!ok) failure = "키 저장 실패: 보드의 인증 저장정보를 읽지 못했습니다. 다시 로그인하고 재시도하세요.";
    }
    if (ok && deleting) { record.key_len = 0; ai_wipe(record.ciphertext, sizeof(record.ciphertext)); ai_wipe(record.iv, sizeof(record.iv)); ai_wipe(record.tag, sizeof(record.tag)); }
    else if (ok) {
        ok = ai_key_encrypt(&record, session->wrapping_key, value->valuestring);
        if (!ok) failure = "키 저장 실패: 보드의 AES-GCM 암호화에 실패했습니다.";
    }
    if (ok) {
        storage_error = ai_record_write(&record);
        ok = storage_error == ESP_OK;
        if (!ok) failure = "키 저장 실패: NVS 저장공간에 기록하지 못했습니다. 보드 로그를 확인하세요.";
    }
    if (!ok && storage_error != ESP_OK) ESP_LOGW(TAG, "AI key storage failed: %s", esp_err_to_name(storage_error));
    if (cJSON_IsString(value)) ai_wipe(value->valuestring, strlen(value->valuestring));
    cJSON_Delete(body); ai_wipe(&record, sizeof(record));
    if (!ok) return ai_error(req, valid ? "500 Internal Server Error" : "400 Bad Request", failure);
    return ai_key_get_handler(req);
}
