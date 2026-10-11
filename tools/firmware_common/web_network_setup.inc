/* Included after decode_wifi_field/restart_task and web_ai_security.inc. */
static bool setup_read_allowed(httpd_req_t *req) {
    return network_setup_client_local(httpd_req_to_sockfd(req)) || ai_session_for(req) != NULL;
}
static esp_err_t setup_json(httpd_req_t *req, const char *code, cJSON *body) {
    char *json = cJSON_PrintUnformatted(body); cJSON_Delete(body);
    if (!json) return httpd_resp_send_500(req);
    httpd_resp_set_status(req, code);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, json); free(json); return err;
}
static esp_err_t setup_error(httpd_req_t *req, const char *code, const char *message) {
    cJSON *body = cJSON_CreateObject(); if (!body) return httpd_resp_send_500(req);
    cJSON_AddStringToObject(body, "message", message); return setup_json(req, code, body);
}
static esp_err_t api_network_get_handler(httpd_req_t *req) {
    if (!setup_read_allowed(req)) return setup_error(req, "401 Unauthorized", "설정용 SoftAP에 연결하거나 관리자 로그인 후 사용하세요.");
    network_settings_t s; network_settings_load(&s);
    cJSON *body = cJSON_CreateObject();
    if (!body) { memset(&s, 0, sizeof(s)); return httpd_resp_send_500(req); }
    cJSON_AddStringToObject(body, "ssid", s.ssid);
    cJSON_AddStringToObject(body, "ip_mode", s.use_static ? "static" : "dhcp");
    cJSON_AddStringToObject(body, "ip", s.ip); cJSON_AddStringToObject(body, "gateway", s.gateway);
    cJSON_AddStringToObject(body, "netmask", s.netmask); cJSON_AddStringToObject(body, "dns", s.dns);
    memset(&s, 0, sizeof(s)); return setup_json(req, "200 OK", body);
}
static esp_err_t api_wifi_scan_handler(httpd_req_t *req) {
    if (!setup_read_allowed(req)) return setup_error(req, "401 Unauthorized", "설정용 SoftAP에 연결하거나 관리자 로그인 후 검색하세요.");
    wifi_scan_item_t items[15]; int count = network_scan_wifi(items, 15);
    if (count < 0) return setup_error(req, "503 Service Unavailable", "Wi-Fi 검색 실패. 이름을 직접 입력하거나 다시 시도하세요.");
    cJSON *body = cJSON_CreateObject(), *networks = cJSON_CreateArray();
    if (!body || !networks) { cJSON_Delete(body); cJSON_Delete(networks); return httpd_resp_send_500(req); }
    cJSON_AddItemToObject(body, "networks", networks);
    for (int i = 0; i < count; i++) {
        cJSON *ap = cJSON_CreateObject(); if (!ap) break;
        cJSON_AddStringToObject(ap, "ssid", items[i].ssid); cJSON_AddNumberToObject(ap, "rssi", items[i].rssi);
        cJSON_AddBoolToObject(ap, "open", items[i].authmode == WIFI_AUTH_OPEN); cJSON_AddItemToArray(networks, ap);
    }
    return setup_json(req, "200 OK", body);
}
static bool setup_field(const char *body, const char *name, char *out, size_t size, bool optional) {
    char encoded[512]; esp_err_t err = httpd_query_key_value(body, name, encoded, sizeof(encoded));
    if (optional && err == ESP_ERR_NOT_FOUND) { out[0] = 0; return true; }
    return err == ESP_OK && decode_wifi_field(out, size, encoded);
}
static esp_err_t wifi_post_handler(httpd_req_t *req) {
    char body[1025] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body))
        return setup_error(req, "400 Bad Request", "설정 요청 크기가 올바르지 않습니다.");
    size_t used = 0;
    while (used < req->content_len) {
        int received = httpd_req_recv(req, body + used, req->content_len - used);
        if (received <= 0) { memset(body, 0, sizeof(body)); return setup_error(req, "400 Bad Request", "설정 수신 실패"); }
        used += received;
    }
    network_settings_t s = { .version = 1 };
    char mode[8] = {0}, admin[128] = {0};
    bool parsed = setup_field(body, "ssid", s.ssid, sizeof(s.ssid), false) &&
        setup_field(body, "pass", s.pass, sizeof(s.pass), true) &&
        setup_field(body, "ip_mode", mode, sizeof(mode), false) &&
        setup_field(body, "admin_pw", admin, sizeof(admin), true);
    bool allowed = parsed && (setup_read_allowed(req) || (admin[0] && verify_admin_password(admin)));
    memset(admin, 0, sizeof(admin));
    if (!allowed) {
        memset(body, 0, sizeof(body)); memset(&s, 0, sizeof(s));
        return setup_error(req, parsed ? "403 Forbidden" : "400 Bad Request", parsed ? "SoftAP 연결 또는 관리자 인증이 필요합니다." : "Wi-Fi 입력값이 올바르지 않습니다.");
    }
    s.use_static = !strcmp(mode, "static");
    parsed = !strcmp(mode, "dhcp") || s.use_static;
    if (parsed && s.use_static) {
        parsed = setup_field(body, "ip", s.ip, sizeof(s.ip), false) &&
            setup_field(body, "gateway", s.gateway, sizeof(s.gateway), false) &&
            setup_field(body, "netmask", s.netmask, sizeof(s.netmask), false) &&
            setup_field(body, "dns", s.dns, sizeof(s.dns), true);
        if (!s.dns[0]) snprintf(s.dns, sizeof(s.dns), "%s", s.gateway);
    }
    memset(body, 0, sizeof(body));
    if (!parsed || !network_settings_valid(&s)) {
        memset(&s, 0, sizeof(s));
        return setup_error(req, "400 Bad Request", "SSID·비밀번호 또는 IP·게이트웨이·마스크·DNS를 확인하세요. 설정용 AP와 서브넷이 겹칠 수 없습니다.");
    }
    /* Allocate the response before changing NVS. */
    cJSON *response = cJSON_CreateObject();
    if (!response) { memset(&s, 0, sizeof(s)); return httpd_resp_send_500(req); }
    char product[96] = {0}, server[96] = {0};
    network_get_mdns_url(product, sizeof(product));
    if (s.use_static) snprintf(server, sizeof(server), "http://%s/", s.ip);
    else snprintf(server, sizeof(server), "%s", product);
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "server_url", server); cJSON_AddStringToObject(response, "product_url", product);
    esp_err_t err = network_save_configuration(&s); memset(&s, 0, sizeof(s));
    if (err != ESP_OK) { cJSON_Delete(response); return setup_error(req, "500 Internal Server Error", "설정 저장 실패. 입력값을 유지하고 다시 시도하세요."); }
    err = setup_json(req, "200 OK", response);
    /* Apply even if the browser disconnects after NVS was committed. */
    if (xTaskCreate(restart_task, "wifi_restart", 2048, NULL, 5, NULL) != pdPASS)
        ESP_LOGE(TAG, "Settings saved; reboot task unavailable. Reboot manually.");
    return err;
}
