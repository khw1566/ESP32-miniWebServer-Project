/* Local administrator recovery remains available in headless web focus mode. */
static const char device_settings_html[] =
"<!doctype html><html lang='ko'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>M4 기기 설정</title><style>body{margin:0;background:#101827;color:#e5edf5;font:16px system-ui}main{max-width:560px;margin:auto;padding:24px}fieldset{border:1px solid #465569;border-radius:12px;margin:18px 0;padding:18px}label{display:block;margin:12px 0}input[type=number],input[type=password]{box-sizing:border-box;width:100%;padding:12px;border-radius:6px}button,a{display:inline-block;padding:12px;margin:6px 4px 6px 0}button{border:0;border-radius:8px;background:#40c4cc;color:#10202a}p{line-height:1.6;overflow-wrap:anywhere}a{color:#75e1e7}#status{white-space:pre-wrap}</style>"
"<main><h1>M4 기기 설정</h1><a href='/admin'>관리자 화면</a><p>관리자 비밀번호로 현재 설정을 불러온 뒤 변경하세요. 운영 모드를 바꾸면 재부팅합니다. BOOT를 누른 채 재시작하면 저장된 집중 모드를 무시하고 화면과 설정 핫스팟을 시작합니다.</p>"
"<form id='form'><label>관리자 비밀번호<input id='password' type='password' maxlength='64' autocomplete='current-password' required></label><button id='load' type='button'>현재 설정 불러오기</button>"
"<fieldset disabled id='controls'><legend>화면</legend><label>밝기 (5–100%)<input id='brightness' type='number' min='5' max='100' value='100' required></label></fieldset>"
"<fieldset disabled id='sound'><legend>소리</legend><label>음량 (0은 무음, 최대 150)<input id='volume' type='number' min='0' max='150' value='40' required></label></fieldset>"
"<fieldset disabled id='performance'><legend>성능과 효과</legend><label>최대 FPS (5–60)<input id='max_fps' type='number' min='5' max='60' value='30' required></label><label><input id='animations' type='checkbox' checked> 애니메이션 효과</label></fieldset>"
"<fieldset disabled id='operation'><legend>운영 모드</legend><label><input id='web_focus' type='checkbox'> 웹서버 집중 모드</label><p>집중 모드에서는 화면이 꺼지며 녹음·오디오·게임·AI·미디어 작업을 시작하지 않습니다. 웹서버·Wi-Fi·SD 파일 제공·외부 도메인 중계는 유지됩니다. 일반 모드로 복귀하려면 이 옵션을 끄고 저장하세요.</p><button type='submit'>저장 및 운영 모드 적용</button></fieldset></form><p id='status' role='status'></p>"
"<script>const $=id=>document.getElementById(id);let busy=false;async function send(read){if(busy)return;busy=true;const body={cur_pw:$('password').value,read};if(!read){for(const k of ['brightness','volume','max_fps'])body[k]=Number($(k).value);for(const k of ['animations','web_focus'])body[k]=$(k).checked;}try{const r=await fetch('/api/device-settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const d=await r.json();if(!r.ok)throw Error(d.message||'설정 처리 실패');for(const k of ['brightness','volume','max_fps'])$(k).value=d[k];for(const k of ['animations','web_focus'])$(k).checked=!!d[k];for(const k of ['controls','sound','performance','operation'])$(k).disabled=false;$('status').textContent=d.message||'설정을 불러왔습니다.';if(!read)$('password').value='';}catch(e){$('status').textContent=e.message;}finally{busy=false;}}$('load').onclick=()=>send(true);$('form').onsubmit=e=>{e.preventDefault();send(false);};</script></main></html>";
static esp_err_t device_settings_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, device_settings_html, sizeof(device_settings_html) - 1);
}
static bool settings_number(const cJSON *body, const char *name, unsigned minimum, unsigned maximum, uint8_t *out) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(body, name);
    if (!cJSON_IsNumber(value) || value->valuedouble < minimum || value->valuedouble > maximum ||
        value->valuedouble != (unsigned)value->valuedouble) return false;
    *out = (uint8_t)value->valuedouble; return true;
}
/* This API has flat scalar fields only; reject nesting before recursive JSON parsing. */
static bool device_settings_json_flat(const char *input) {
    unsigned depth = 0; bool quoted = false, escaped = false;
    for (size_t i = 0; input[i]; ++i) {
        char c = input[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 1) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}
static esp_err_t device_settings_post(httpd_req_t *req) {
    if (req->content_len < 1 || req->content_len > 512) return setup_error(req, "400 Bad Request", "요청 크기를 확인하세요.");
    char input[513]; size_t used = 0;
    while (used < req->content_len) {
        int count = httpd_req_recv(req, input + used, req->content_len - used);
        if (count <= 0) return setup_error(req, "400 Bad Request", "설정 요청 수신 실패");
        used += count;
    }
    input[used] = 0;
    if (!device_settings_json_flat(input)) {
        memset(input, 0, sizeof(input)); return setup_error(req, "400 Bad Request", "설정 요청 형식을 확인하세요.");
    }
    cJSON *body = cJSON_Parse(input); memset(input, 0, sizeof(input));
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "cur_pw");
    if (!cJSON_IsString(password) || strlen(password->valuestring) > 64 || !verify_admin_password(password->valuestring)) {
        cJSON_Delete(body); return setup_error(req, "403 Forbidden", "관리자 비밀번호를 확인하세요.");
    }
    m4_preferences_t p, before; m4_preferences_get(&before); p = before;
    bool read_only = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "read"));
    const char *message = "설정을 불러왔습니다.";
    if (!read_only) {
        if (m4_preferences_restarting()) { cJSON_Delete(body); return setup_error(req, "409 Conflict", "재시작 중입니다. 잠시 후 다시 접속하세요."); }
        const cJSON *animations = cJSON_GetObjectItemCaseSensitive(body, "animations");
        const cJSON *focus = cJSON_GetObjectItemCaseSensitive(body, "web_focus");
        bool valid = settings_number(body, "brightness", 5, 100, &p.brightness) &&
            settings_number(body, "max_fps", 5, 60, &p.max_fps) && settings_number(body, "volume", 0, 150, &p.volume) &&
            cJSON_IsBool(animations) && cJSON_IsBool(focus);
        if (!valid) { cJSON_Delete(body); return setup_error(req, "400 Bad Request", "설정 범위를 확인하세요."); }
        p.animations = cJSON_IsTrue(animations); p.web_focus = cJSON_IsTrue(focus);
        if (p.volume) p.last_volume = p.volume;
        if (p.web_focus != before.web_focus || p.web_focus != m4_preferences_web_active()) {
            m4_mode_result_t result = m4_preferences_restart_with(&p);
            if (result != M4_MODE_OK) {
                cJSON_Delete(body);
                return setup_error(req, result == M4_MODE_BUSY ? "409 Conflict" : "500 Internal Server Error",
                    result == M4_MODE_BUSY ? "녹음·오디오·AI 작업과 SD 업로드를 마친 뒤 다시 적용하세요." :
                    result == M4_MODE_RESTART_FAILED ? "설정은 저장됐지만 재시작을 시작하지 못했습니다. 직접 재시작하세요." : "설정 저장 실패. 다시 시도하세요.");
            }
            message = "설정을 저장했습니다. 운영 모드 적용을 위해 재시작합니다.";
        } else {
            if (m4_preferences_save(&p) != ESP_OK) { cJSON_Delete(body); return setup_error(req, "500 Internal Server Error", "설정 저장 실패. 다시 시도하세요."); }
            if (!m4_preferences_web_active()) m4_audio_volume(p.volume);
            message = "설정을 저장했습니다.";
        }
    }
    cJSON_Delete(body);
    cJSON *reply = cJSON_CreateObject(); if (!reply) return httpd_resp_send_500(req);
    cJSON_AddNumberToObject(reply, "brightness", p.brightness); cJSON_AddNumberToObject(reply, "max_fps", p.max_fps);
    cJSON_AddNumberToObject(reply, "volume", p.volume); cJSON_AddBoolToObject(reply, "animations", p.animations);
    cJSON_AddBoolToObject(reply, "web_focus", p.web_focus); cJSON_AddStringToObject(reply, "message", message);
    return setup_json(req, "200 OK", reply);
}
