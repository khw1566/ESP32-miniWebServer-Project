/* Small bounded-buffer media responses, including single byte ranges. */
static esp_err_t send_media_file(httpd_req_t *req, FILE *f, const char *mime) {
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return httpd_resp_send_500(req); }
    long size = ftell(f), start = 0, end = size - 1;
    if (size < 0) { fclose(f); return httpd_resp_send_500(req); }
    bool partial = false;
    size_t header_len = httpd_req_get_hdr_value_len(req, "Range");
    if (header_len) {
        char range[96], *p, *tail;
        if (header_len >= sizeof(range) || httpd_req_get_hdr_value_str(req, "Range", range, sizeof(range)) != ESP_OK) goto invalid_range;
        if (strncmp(range, "bytes=", 6) != 0 || strchr(range, ',')) goto invalid_range;
        p = range + 6;
        if (*p == '-') {
            p++;
            if (*p < '0' || *p > '9') goto invalid_range;
            long suffix = strtol(p, &tail, 10);
            if (*tail || suffix <= 0) goto invalid_range;
            start = suffix >= size ? 0 : size - suffix;
        } else {
            if (*p < '0' || *p > '9') goto invalid_range;
            start = strtol(p, &tail, 10);
            if (*tail != '-') goto invalid_range;
            p = tail + 1;
            if (*p) {
                if (*p < '0' || *p > '9') goto invalid_range;
                end = strtol(p, &tail, 10);
                if (*tail) goto invalid_range;
            }
        }
        if (size == 0 || start < 0 || start >= size || end < start) goto invalid_range;
        if (end >= size) end = size - 1;
        partial = true;
    }
    if (fseek(f, start, SEEK_SET) != 0) { fclose(f); return httpd_resp_send_500(req); }
    httpd_resp_set_type(req, mime);
    httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");
    char content_range[80];
    if (partial) {
        snprintf(content_range, sizeof(content_range), "bytes %ld-%ld/%ld", start, end, size);
        httpd_resp_set_status(req, "206 Partial Content");
        httpd_resp_set_hdr(req, "Content-Range", content_range);
    }
    char chunk[1024];
    long remaining = size ? end - start + 1 : 0;
    while (remaining > 0) {
        size_t amount = remaining > (long)sizeof(chunk) ? sizeof(chunk) : (size_t)remaining;
        size_t got = fread(chunk, 1, amount, f);
        if (got == 0 || httpd_resp_send_chunk(req, chunk, got) != ESP_OK) { fclose(f); return ESP_FAIL; }
        remaining -= (long)got;
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
invalid_range:
    fclose(f);
    {
        char unsatisfied[64];
        snprintf(unsatisfied, sizeof(unsatisfied), "bytes */%ld", size);
        httpd_resp_set_status(req, "416 Range Not Satisfiable");
        httpd_resp_set_hdr(req, "Content-Range", unsatisfied);
        return httpd_resp_send(req, NULL, 0);
    }
}
