/* Included by m3_tube.c. One cancellable download owns its HTTP client and SD lock. */
static m3_tube_download_t download_state;
static atomic_bool download_cancel;
static char download_id[12];
static char download_title[31];
static unsigned download_quality;
void m3_tube_download_state(m3_tube_download_t *out) {
    portENTER_CRITICAL(&service_lock); *out = download_state; portEXIT_CRITICAL(&service_lock);
}
void m3_tube_download_cancel(void) { atomic_store(&download_cancel, true); }
static bool download_stopped(void) { return atomic_load(&download_cancel) || m3_preferences_restarting(); }
static void download_progress(const char *message, uint32_t bytes, uint32_t total) {
    portENTER_CRITICAL(&service_lock);
    download_state.bytes = bytes; download_state.total = total;
    download_state.percent = total ? (unsigned)((uint64_t)bytes * 100 / total) : 0;
    snprintf(download_state.message, sizeof(download_state.message), "%s", message);
    portEXIT_CRITICAL(&service_lock);
}
static int download_json(esp_http_client_handle_t http, sink_t *sink, uint8_t *buffer,
                         const char *url, esp_http_client_method_t method) {
    esp_http_client_set_method(http, method);
    return http_get(http, sink, buffer, 1023, url);
}
static void download_worker(void *unused) {
    (void)unused;
    const char *message = "Download failed";
    bool owned = false, locked = false, saved = false, created = false;
    uint32_t bytes = 0, total = 0;
    FILE *file = NULL;
    esp_http_client_handle_t http = NULL;
    uint8_t *buffer = NULL;
    char url[320], job[33] = "", filename[64], final[160], partial[168];
    if (download_title[0]) snprintf(filename, sizeof(filename), "%s_%.11s_%s.mp4", download_title, download_id, quality_name(download_quality));
    else snprintf(filename, sizeof(filename), "%.11s_%s.mp4", download_id, quality_name(download_quality));
    snprintf(final, sizeof(final), SD_MEDIA_ROOT M3_TUBE_DOWNLOAD_FOLDER "/%s", filename);
    snprintf(partial, sizeof(partial), "%s.part", final);
    sink_t sink = {0};
    m3_relay_profile_t relay = {0};
    if (!sd_media_ready()) { message = "SD unavailable or busy"; goto done; }
    if (network_get_mode() != NET_MODE_STA_CONNECTED) { message = "Connect Wi-Fi first"; goto done; }
    if (!lease_tls(1500, &owned)) { message = "HTTPS busy; stop radio or video and retry"; goto done; }
    if (!m3_relay_select(&relay)) { message = "Relay connection failed"; goto done; }
    buffer = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) { message = "Video buffer allocation failed"; goto done; }
    snprintf(url, sizeof(url), "%s/api/download", relay.url);
    esp_http_client_config_t config = {.url = url, .timeout_ms = 5000, .disable_auto_redirect = true,
        .buffer_size = 4096, .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = sink_event, .user_data = &sink};
    http = esp_http_client_init(&config);
    if (!http) { message = "Relay connection failed"; goto done; }
    esp_http_client_set_header(http, "x-m3-token", relay.token);
    esp_http_client_set_header(http, "Accept-Encoding", "identity");
    esp_http_client_set_header(http, "Content-Length", "0");
    snprintf(url, sizeof(url), "%s/api/download?id=%.11s&quality=%s", relay.url, download_id, quality_name(download_quality));
    int status = download_json(http, &sink, buffer, url, HTTP_METHOD_POST);
    if (status != 202) {
        message = status == 409 ? "PC download is busy" : status == 401 ? "Relay token rejected; check SD relay settings" :
            status == 404 ? "Update PC relay for quality and downloads" : "Download failed"; goto done;
    }
    buffer[sink.length] = 0;
    cJSON *root = cJSON_Parse((char *)buffer);
    const cJSON *value = root ? cJSON_GetObjectItem(root, "job") : NULL;
    if (cJSON_IsString(value) && strlen(value->valuestring) == 32 && strspn(value->valuestring, "0123456789abcdef") == 32)
        snprintf(job, sizeof(job), "%s", value->valuestring);
    cJSON_Delete(root);
    if (!job[0]) goto done;
    esp_http_client_delete_header(http, "Content-Length");
    download_progress("Preparing video on PC", 0, 0);
    int64_t deadline = esp_timer_get_time() + 660LL * 1000000;
    bool ready = false;
    while (!download_stopped() && esp_timer_get_time() < deadline) {
        snprintf(url, sizeof(url), "%s/api/download/status?job=%s", relay.url, job);
        status = download_json(http, &sink, buffer, url, HTTP_METHOD_GET);
        if (status != 200) { message = "Relay connection failed"; goto done; }
        buffer[sink.length] = 0;
        root = cJSON_Parse((char *)buffer); value = root ? cJSON_GetObjectItem(root, "state") : NULL;
        ready = cJSON_IsString(value) && !strcmp(value->valuestring, "ready");
        bool failed = !cJSON_IsString(value) || (!ready && strcmp(value->valuestring, "preparing"));
        if (failed) {
            const cJSON *detail = root ? cJSON_GetObjectItem(root, "error") : NULL;
            if (cJSON_IsString(detail)) ESP_LOGW(TAG, "download preparation: %.120s", detail->valuestring);
        }
        cJSON_Delete(root);
        if (failed) { message = "PC video preparation failed; check relay log"; goto done; }
        if (ready) break;
        for (unsigned i = 0; i < 10 && !download_stopped(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (download_stopped()) { message = "Download cancelled"; goto done; }
    if (!ready) { message = "PC video preparation timed out"; goto done; }
    if (!(locked = sd_media_admin_lock())) { message = "SD unavailable or busy"; goto done; }
    if (mkdir(SD_MEDIA_ROOT M3_TUBE_DOWNLOAD_FOLDER, 0775) != 0 && errno != EEXIST) { message = "SD video write failed"; goto done; }
    struct stat existing;
    if (stat(final, &existing) == 0) { message = "Video already saved on SD"; saved = true; goto done; }
    snprintf(url, sizeof(url), "%s/api/download/file?job=%s", relay.url, job);
    sink.data = NULL;
    esp_http_client_set_method(http, HTTP_METHOD_GET); esp_http_client_set_url(http, url);
    if (esp_http_client_open(http, 0) != ESP_OK || esp_http_client_fetch_headers(http) < 0 || esp_http_client_get_status_code(http) != 200) goto done;
    int64_t length = esp_http_client_get_content_length(http);
    if (length < 12 || length > 512LL * 1024 * 1024) { message = "Invalid download size"; goto done; }
    total = (uint32_t)length;
    uint64_t capacity, free_bytes;
    if (esp_vfs_fat_info(SD_MEDIA_ROOT, &capacity, &free_bytes) != ESP_OK || free_bytes < (uint64_t)total + 1024 * 1024) {
        message = "Not enough SD space"; goto done;
    }
    /* Never overwrite a completed file. An interrupted .part can be safely retried. */
    unlink(partial);
    int fd = open(partial, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) { message = "SD video write failed"; goto done; }
    created = true; file = fdopen(fd, "wb");
    if (!file) { close(fd); message = "SD video write failed"; goto done; }
    int64_t last_data = esp_timer_get_time();
    while (bytes < total && !download_stopped()) {
        unsigned step = total - bytes < 8192 ? total - bytes : 8192;
        int got = esp_http_client_read(http, (char *)buffer, step);
        if (got <= 0) {
            if (got < 0 && got != -ESP_ERR_HTTP_EAGAIN) break;
            if (esp_http_client_is_complete_data_received(http) || esp_timer_get_time() - last_data > 30000000) break;
            vTaskDelay(pdMS_TO_TICKS(20)); continue;
        }
        if (fwrite(buffer, 1, got, file) != (size_t)got) { message = "SD video write failed"; goto done; }
        bytes += (unsigned)got; last_data = esp_timer_get_time();
        download_progress("Saving video to SD", bytes, total);
    }
    if (download_stopped()) { message = "Download cancelled"; goto done; }
    if (bytes != total) { message = "Download incomplete; retry"; goto done; }
    bool committed = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) committed = false;
    file = NULL;
    if (!committed || rename(partial, final) != 0) { message = "SD video write failed"; goto done; }
    created = false; saved = true; message = "Video saved on SD";
done:
    if (file) fclose(file);
    if (created) unlink(partial);
    if (locked) { if (saved) sd_media_changed(); sd_media_admin_unlock(); }
    if (http) {
        esp_http_client_close(http);
        if (job[0] && !saved) {
            snprintf(url, sizeof(url), "%s/api/download/status?job=%s", relay.url, job);
            download_json(http, &sink, buffer, url, HTTP_METHOD_DELETE);
        }
        esp_http_client_cleanup(http);
    }
    free(buffer);
    if (owned) https_memory_exclusive_end();
    if (download_stopped()) message = "Download cancelled";
    download_progress(message, bytes, total);
    portENTER_CRITICAL(&service_lock);
    download_state.saved = saved;
    if (saved) snprintf(download_state.filename, sizeof(download_state.filename), "%s", filename);
    download_state.busy = false;
    portEXIT_CRITICAL(&service_lock);
    vTaskDelete(NULL);
}
bool m3_tube_download(const char *id, const char *title) {
    if (!video_id_valid(id) || !m3_tube_configured() || m3_preferences_restarting()) return false;
    portENTER_CRITICAL(&service_lock);
    if (download_state.busy) { portEXIT_CRITICAL(&service_lock); return false; }
    memset(&download_state, 0, sizeof(download_state)); download_state.busy = true;
    snprintf(download_state.message, sizeof(download_state.message), "Preparing video on PC");
    snprintf(download_id, sizeof(download_id), "%s", id); download_quality = atomic_load(&selected_quality);
    /* A new derived name, with the video ID retained to avoid title collisions. */
    m3_utf8_copy(download_title, sizeof(download_title), title ? title : "");
    for (char *p = download_title; *p; ++p)
        if ((unsigned char)*p < 32 || strchr("\\/:*?\"<>|", *p) || (p == download_title && *p == '.')) *p = '_';
    atomic_store(&download_cancel, false);
    portEXIT_CRITICAL(&service_lock);
    if (xTaskCreatePinnedToCore(download_worker, "m3_tube_save", 10240, NULL, 3, NULL, 0) == pdPASS) return true;
    download_progress("Video task allocation failed", 0, 0);
    portENTER_CRITICAL(&service_lock); download_state.busy = false; portEXIT_CRITICAL(&service_lock);
    return false;
}
