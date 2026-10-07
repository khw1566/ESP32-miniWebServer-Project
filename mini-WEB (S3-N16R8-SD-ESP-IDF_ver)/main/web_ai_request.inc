/* Request parsing failures are local to the request, including async tasks. */
typedef enum {
    AI_JSON_OK, AI_JSON_SIZE, AI_JSON_TYPE, AI_JSON_MEMORY,
    AI_JSON_TIMEOUT, AI_JSON_RECEIVE, AI_JSON_COMPLEXITY, AI_JSON_SYNTAX
} ai_json_error_t;
static const char *ai_json_error_status(ai_json_error_t error) {
    switch (error) {
        case AI_JSON_MEMORY: return "503 Service Unavailable";
        case AI_JSON_TIMEOUT: return "408 Request Timeout";
        case AI_JSON_TYPE: return "415 Unsupported Media Type";
        case AI_JSON_SIZE: case AI_JSON_COMPLEXITY: return "413 Payload Too Large";
        default: return "400 Bad Request";
    }
}
static const char *ai_json_error_message(ai_json_error_t error) {
    switch (error) {
        case AI_JSON_MEMORY: return "요청을 해석할 보드 메모리가 부족합니다. 잠시 후 다시 요청하거나 대화를 줄여 주세요.";
        case AI_JSON_TIMEOUT: return "요청 본문 수신 시간이 초과됐습니다. Wi-Fi 연결을 확인하고 다시 보내세요.";
        case AI_JSON_RECEIVE: return "요청을 끝까지 수신하지 못했습니다. 연결을 확인하고 다시 보내세요.";
        case AI_JSON_TYPE: return "요청의 Content-Type은 application/json이어야 합니다.";
        case AI_JSON_SIZE: return "요청이 비어 있거나 최대 48 KiB를 초과했습니다. 요청을 나누어 보내세요.";
        case AI_JSON_COMPLEXITY: return "요청 데이터의 항목 수 또는 중첩 깊이 한도를 초과했습니다. 작업을 나누어 보내세요.";
        default: return "요청 JSON을 해석하지 못했습니다. 페이지를 새로고침한 뒤 다시 요청하세요.";
    }
}
static cJSON *ai_read_json_buffer_ex(httpd_req_t *req, size_t limit, size_t reserve,
                                    char **reuse, ai_json_error_t *failure) {
    *reuse = NULL;
    ai_json_error_t reason = AI_JSON_SYNTAX;
    char *body = NULL;
    size_t capacity = 0;
    if (failure) *failure = AI_JSON_OK;
    if (req->content_len <= 0 || (size_t)req->content_len > limit) { reason = AI_JSON_SIZE; goto failed; }
    char type[80] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strncasecmp(type, "application/json", 16) ||
        (type[16] && type[16] != ';' && type[16] != ' ')) { reason = AI_JSON_TYPE; goto failed; }
    capacity = (size_t)req->content_len + reserve + 1;
    body = malloc(capacity);
    if (!body) { reason = AI_JSON_MEMORY; goto failed; }
    int done = 0, timeouts = 0;
    while (done < req->content_len) {
        int count = httpd_req_recv(req, body + done, req->content_len - done);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts < 3) continue;
            reason = AI_JSON_TIMEOUT; goto failed;
        }
        if (count <= 0) { reason = AI_JSON_RECEIVE; goto failed; }
        done += count;
    }
    body[done] = 0;
    int depth = 0; size_t nodes = 1;
    bool quoted = false, escape = false;
    if (memchr(body, 0, done)) goto failed;
    for (int i = 0; i < done; ++i) {
        char c = body[i];
        if (quoted) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            ++nodes; if (++depth > 16) { reason = AI_JSON_COMPLEXITY; goto failed; }
        } else if (c == '}' || c == ']') { if (--depth < 0) goto failed; }
        else if (c == ':' || c == ',') ++nodes;
        if (nodes > 384) { reason = AI_JSON_COMPLEXITY; goto failed; }
    }
    if (quoted || depth) goto failed;
    /* Raw input, cJSON nodes and decoded strings coexist during parsing.
     * General malloc can use PSRAM on the N16R8 profile. */
    size_t available = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (available < (size_t)done + nodes * (sizeof(cJSON) + 32) + 16 * 1024) {
        reason = AI_JSON_MEMORY; goto failed;
    }
    cJSON *value = cJSON_ParseWithLengthOpts(body, done + 1, NULL, true);
    if (!value) goto failed;
    if (reserve) *reuse = body;
    else { ai_wipe(body, capacity); free(body); }
    return value;
failed:
    if (body) { ai_wipe(body, capacity); free(body); }
    if (failure) *failure = reason;
    return NULL;
}
static cJSON *ai_read_json(httpd_req_t *req, size_t limit) {
    char *unused;
    return ai_read_json_buffer_ex(req, limit, 0, &unused, NULL);
}
