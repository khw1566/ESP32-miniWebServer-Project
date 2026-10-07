/* HTTPS-only provider proxy. A separate task keeps the HTTP server responsive. */
typedef struct {
    httpd_req_t *req;
    unsigned char wrapping_key[32];
    char model[81];
} ai_proxy_job_t;
static atomic_bool s_ai_proxy_busy;
static bool s_ai_sntp_started;

static void ai_proxy_task(void *arg) {
    ai_proxy_job_t *job = arg;
    httpd_req_t *req = job->req;
    ai_security_record_t record = {0};
    char api_key[256] = {0}, url[256];
    char *payload = NULL;
    cJSON *body = NULL;
    esp_http_client_handle_t client = NULL;
    bool response_started = false, completed = false, memory_lease = false;
    size_t payload_capacity = (size_t)req->content_len + 513;
    const char *error = "AI 요청을 처리하지 못했습니다.";
    const char *status = "502 Bad Gateway";
    /* Wait in the asynchronous task. The local HTTP server remains available,
     * and the relay finishes its current poll before releasing TLS memory. */
    memory_lease = https_memory_exclusive_begin(65000);
    if (!memory_lease) {
        status = "503 Service Unavailable"; error = "클라우드 연결 정리를 기다리는 중입니다. 잠시 후 다시 요청하세요."; goto done;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < (size_t)req->content_len * 2 + 32 * 1024 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < payload_capacity) {
        status = "503 Service Unavailable"; error = "보드 메모리가 부족합니다. 대화를 줄이거나 새 대화를 시작하세요."; goto done;
    }
    if (ai_record_read(&record) != ESP_OK || !ai_key_decrypt(&record, job->wrapping_key, api_key)) {
        status = "409 Conflict"; error = "먼저 유효한 API 키를 보드에 저장하세요."; goto done;
    }
    body = ai_read_json_buffer(req, 48 * 1024, 512, &payload);
    if (!cJSON_IsObject(body) || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(body, "contents"))) {
        status = "400 Bad Request"; error = "AI 요청 형식이 올바르지 않거나 최대 48 KiB를 초과했습니다."; goto done;
    }
    cJSON *config = cJSON_GetObjectItemCaseSensitive(body, "generationConfig");
    if (!cJSON_IsObject(config)) {
        cJSON_DeleteItemFromObjectCaseSensitive(body, "generationConfig");
        config = cJSON_AddObjectToObject(body, "generationConfig");
    }
    if (!config) goto done;
    cJSON_DeleteItemFromObjectCaseSensitive(config, "maxOutputTokens");
    if (!cJSON_AddNumberToObject(config, "maxOutputTokens", 8192) ||
        !cJSON_PrintPreallocated(body, payload, (int)payload_capacity, false)) {
        status = "503 Service Unavailable"; error = "메모리가 부족합니다. 새 대화를 시작하거나 요청을 짧게 줄이세요."; goto done;
    }
    cJSON_Delete(body); body = NULL;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 64 * 1024 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 24 * 1024) {
        status = "503 Service Unavailable"; error = "HTTPS 연결에 필요한 메모리가 부족합니다. 대화를 줄여 다시 요청하세요."; goto done;
    }
    /* The SDK debug logger can print the complete outgoing headers. */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
    esp_log_level_set("HTTP_HEADER", ESP_LOG_WARN);
    if (time(NULL) < 1704067200) {
        if (!s_ai_sntp_started) {
            esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_err_t time_err = esp_netif_sntp_init(&time_config);
            if (time_err == ESP_OK || time_err == ESP_ERR_INVALID_STATE) s_ai_sntp_started = true;
        }
        if (s_ai_sntp_started) esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
        if (time(NULL) < 1704067200) {
            error = "보드 시간을 동기화하지 못했습니다. 인터넷 연결과 시간 서버 접속을 확인하세요."; goto done;
        }
    }
    snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", job->model);
    esp_http_client_config_t options = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 45000,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .buffer_size = 1024, .buffer_size_tx = 1024
    };
    client = esp_http_client_init(&options);
    if (!client) goto done;
    if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
        esp_http_client_set_header(client, "x-goog-api-key", api_key) != ESP_OK) goto done;
    size_t length = strlen(payload);
    if (esp_http_client_open(client, (int)length) != ESP_OK) {
        error = "안전한 HTTPS 연결에 실패했습니다. 보드의 Wi-Fi·인터넷 연결과 시간을 확인하세요."; goto done;
    }
    size_t sent = 0;
    while (sent < length) {
        int count = esp_http_client_write(client, payload + sent, (int)(length - sent));
        if (count <= 0) goto done;
        sent += count;
    }
    ai_wipe(payload, payload_capacity); free(payload); payload = NULL;
    if (esp_http_client_fetch_headers(client) < 0) goto done;
    int provider_status = esp_http_client_get_status_code(client);
    if (provider_status < 200 || provider_status >= 300) {
        /* Do not relay provider error bodies, which may contain credentials. */
        if (provider_status == 401 || provider_status == 403) { error = "Google이 API 키를 거부했습니다. 키와 API 사용 제한을 확인하세요."; }
        else if (provider_status == 429) { status = "429 Too Many Requests"; error = "Google API 사용 한도를 초과했습니다. 잠시 후 다시 요청하세요."; }
        else if (provider_status == 400 || provider_status == 404) { error = "Google이 모델 또는 요청을 거부했습니다. 모델 ID와 대화 크기를 확인하세요."; }
        goto done;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    char chunk[1024]; size_t received = 0;
    while (true) {
        int count = esp_http_client_read(client, chunk, sizeof(chunk));
        if (count < 0) goto done;
        if (count == 0) {
            if (!esp_http_client_is_complete_data_received(client)) goto done;
            if (!received) { error = "Google에서 빈 응답을 받았습니다."; goto done; }
            completed = httpd_resp_send_chunk(req, NULL, 0) == ESP_OK; goto done;
        }
        received += count;
        if (received > 128 * 1024) goto done;
        response_started = true;
        if (httpd_resp_send_chunk(req, chunk, count) != ESP_OK) goto done;
    }
done:
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    if (payload) { ai_wipe(payload, payload_capacity); free(payload); }
    cJSON_Delete(body);
    ai_wipe(api_key, sizeof(api_key)); ai_wipe(&record, sizeof(record));
    ESP_LOGI(TAG, "[AI] Complete: free=%u, largest=%u, stack_min=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (!response_started && !completed) ai_error(req, status, error);
    /* A truncated response must not be reused as complete JSON. */
    bool close_socket = response_started && !completed;
    int sockfd = httpd_req_to_sockfd(req);
    httpd_req_async_handler_complete(req);
    if (close_socket && s_server) httpd_sess_trigger_close(s_server, sockfd);
    ai_wipe(job, sizeof(*job)); free(job);
    if (memory_lease) https_memory_exclusive_end();
    atomic_store(&s_ai_proxy_busy, false);
    vTaskDelete(NULL);
}

static esp_err_t ai_chat_post_handler(httpd_req_t *req) {
    ai_session_t *session = ai_session_for(req);
    if (!session) return ai_error(req, "401 Unauthorized", "AI 로그인이 필요하거나 세션이 만료되었습니다.");
    if (atomic_load(&s_ai_proxy_busy)) return ai_error(req, "429 Too Many Requests", "다른 AI 요청을 처리 중입니다. 완료 후 다시 요청하세요.");
    if (req->content_len <= 0 || req->content_len > 48 * 1024)
        return ai_error(req, "413 Payload Too Large", "대화가 48 KiB를 초과했습니다. 새 대화를 시작하세요.");
    /* Payload admission happens after the relay releases its connection. */
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 20 * 1024)
        return ai_error(req, "503 Service Unavailable", "보드 메모리가 부족합니다. 잠시 후 다시 요청하세요.");
    char query[128] = {0}, model[81] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "model", model, sizeof(model)) != ESP_OK || !*model)
        return ai_error(req, "400 Bad Request", "모델 ID를 선택하거나 입력하세요.");
    for (const char *p = model; *p; ++p)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.') || strstr(model, ".."))
            return ai_error(req, "400 Bad Request", "모델 ID 형식이 올바르지 않습니다.");
    ai_proxy_job_t *job = calloc(1, sizeof(*job));
    if (!job) return httpd_resp_send_500(req);
    memcpy(job->wrapping_key, session->wrapping_key, 32); snprintf(job->model, sizeof(job->model), "%s", model);
    esp_err_t err = httpd_req_async_handler_begin(req, &job->req);
    if (err != ESP_OK) { ai_wipe(job, sizeof(*job)); free(job); return httpd_resp_send_500(req); }
    atomic_store(&s_ai_proxy_busy, true);
    if (xTaskCreate(ai_proxy_task, "ai_https", 16384, job, 4, NULL) != pdPASS) {
        ai_error(job->req, "503 Service Unavailable", "AI 처리에 필요한 메모리를 확보하지 못했습니다.");
        httpd_req_async_handler_complete(job->req); ai_wipe(job, sizeof(*job)); free(job); atomic_store(&s_ai_proxy_busy, false);
    }
    return ESP_OK;
}
