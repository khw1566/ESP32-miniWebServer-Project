/* Included by cloud_relay.c; all HTTP/TLS operations stay on the relay task. */
#define RELAY_STREAM_BUFFER_SIZE (16U * 1024U)
#define RELAY_STREAM_LIMIT (64U * 1024U * 1024U)
#define RELAY_STREAM_IDLE_US (15LL * 1000000)
#define RELAY_STREAM_MAX_US (10LL * 60 * 1000000)

static bool relay_stream_alive(esp_http_client_handle_t cloud, int64_t idle, int64_t maximum) {
    int64_t now = esp_timer_get_time();
    if (now < idle && now < maximum && !https_memory_exclusive_pending() &&
        network_get_mode() == NET_MODE_STA_CONNECTED && web_server_is_running()) return true;
    esp_http_client_cancel_request(cloud);
    return false;
}

static bool relay_stream_write(esp_http_client_handle_t cloud, const uint8_t *data, size_t count,
                               int64_t *idle, int64_t maximum) {
    size_t sent = 0;
    while (sent < count) {
        if (!relay_stream_alive(cloud, *idle, maximum)) return false;
        int wrote = esp_http_client_write(cloud, (const char *)data + sent, count - sent);
        if (wrote == -ESP_ERR_HTTP_EAGAIN || wrote == 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        if (wrote < 0 || (size_t)wrote > count - sent) return false;
        sent += (size_t)wrote;
        *idle = esp_timer_get_time() + RELAY_STREAM_IDLE_US;
        atomic_store(&last_ok_seconds, (uint32_t)(esp_timer_get_time()/1000000));
    }
    return true;
}

static bool relay_stream_send(esp_http_client_handle_t cloud, reply_t *reply,
                              const char *id, web_public_stream_t *stream) {
    uint8_t *buffer = heap_caps_malloc(RELAY_STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) return false;
    bool ok = false;
    int64_t started = esp_timer_get_time(), reading_us = 0, writing_us = 0;
    int64_t maximum = started + RELAY_STREAM_MAX_US, idle = started + RELAY_STREAM_IDLE_US;
    char url[256], status[12];
    int n = snprintf(url,sizeof(url),"%s/v2/devices/%s/result/%s/stream",CONFIG_MINI_WEB_RELAY_URL,device_id,id);
    if (n < 0 || (size_t)n >= sizeof(url) || stream->length > RELAY_STREAM_LIMIT) goto done;
    snprintf(status,sizeof(status),"%d",stream->status);
    memset(reply,0,sizeof(*reply));
    if (esp_http_client_set_url(cloud,url) != ESP_OK ||
        esp_http_client_set_method(cloud,HTTP_METHOD_POST) != ESP_OK ||
        esp_http_client_set_post_field(cloud,NULL,0) != ESP_OK ||
        esp_http_client_set_header(cloud,"X-Relay-Status",status) != ESP_OK ||
        esp_http_client_set_header(cloud,"Content-Type",stream->mime) != ESP_OK) goto done;
    esp_http_client_delete_header(cloud,"X-Sequence");
    if (stream->content_range[0] && esp_http_client_set_header(cloud,"Content-Range",stream->content_range) != ESP_OK) goto done;
    if ((stream->file || stream->memory_file) && esp_http_client_set_header(cloud,"Accept-Ranges","bytes") != ESP_OK) goto done;
    if (!relay_stream_alive(cloud,idle,maximum)) goto done;
    esp_http_client_set_timeout_ms(cloud,3000);
    if (esp_http_client_open(cloud,(int)stream->length) != ESP_OK) goto done;
    esp_http_client_set_timeout_ms(cloud,250);
    while (stream->offset < stream->length) {
        if (!relay_stream_alive(cloud,idle,maximum)) goto done;
        int64_t before = esp_timer_get_time();
        size_t count = web_server_public_read(stream,buffer,RELAY_STREAM_BUFFER_SIZE);
        reading_us += esp_timer_get_time() - before;
        if (!count) goto done;
        before = esp_timer_get_time();
        if (!relay_stream_write(cloud,buffer,count,&idle,maximum)) goto done;
        writing_us += esp_timer_get_time() - before;
    }
    int64_t headers;
    do {
        if (!relay_stream_alive(cloud,idle,maximum)) goto done;
        headers = esp_http_client_fetch_headers(cloud);
        if (headers == -ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(1));
    } while (headers == -ESP_ERR_HTTP_EAGAIN);
    if (headers < 0 || reply->overflow) goto done;
    char discard[128];
    while (!esp_http_client_is_complete_data_received(cloud)) {
        if (!relay_stream_alive(cloud,idle,maximum)) goto done;
        int count = esp_http_client_read(cloud,discard,sizeof(discard));
        if (reply->overflow || (count < 0 && count != -ESP_ERR_HTTP_EAGAIN)) goto done;
        if (count <= 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    ok = esp_http_client_get_status_code(cloud) == 204;
done:
    /* A failed/partial POST must be closed before the next request. */
    if (!ok) esp_http_client_close(cloud);
    esp_http_client_delete_header(cloud,"X-Relay-Status");
    esp_http_client_delete_header(cloud,"Content-Range");
    esp_http_client_delete_header(cloud,"Accept-Ranges");
    free(buffer);
    int64_t elapsed = esp_timer_get_time() - started;
    ESP_LOGI("CLOUD_RELAY", "stream-v2 ok=%d bytes=%u elapsed_ms=%llu read_ms=%llu write_ms=%llu KBps=%llu",
        ok,(unsigned)stream->offset,(unsigned long long)(elapsed/1000),
        (unsigned long long)(reading_us/1000),(unsigned long long)(writing_us/1000),
        (unsigned long long)(elapsed > 0 ? (uint64_t)stream->offset * 1000 / elapsed : 0));
    return ok;
}
