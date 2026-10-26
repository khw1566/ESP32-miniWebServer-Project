/* One producer owns allocation/HTTP/SD; consumers pin immutable completed slots.
 * Control, locks and task stack stay internal. Compressed data lives in PSRAM. */
enum { MUSIC_CACHE_BYTES = 8 * 1024 * 1024, MUSIC_CACHE_RESERVE = 4 * 1024 * 1024, MUSIC_DECODE_BYTES = 32768 };
enum { MUSIC_EMPTY, MUSIC_LOADING, MUSIC_READY, MUSIC_FAILED };
typedef struct {
    char id[12], message[96], title[160], channel[64], channel_id[25], job[33]; uint8_t *data;
    unsigned state, refs, bytes, total, duration;
} music_slot_t;
static M4_PSRAM_BSS music_slot_t music_slots[2];
static SemaphoreHandle_t music_lock;
static atomic_bool music_auto_next=ATOMIC_VAR_INIT(true);
static char music_wanted[2][12], music_save_id[12], music_save_title[31];
void m4_tube_music_autonext(bool enabled) { atomic_store(&music_auto_next,enabled); }
static void music_follow(const char *id) {
    if (!music_lock || !atomic_load(&music_auto_next)) return;
    char next[12]="";
    xSemaphoreTake(music_lock,portMAX_DELAY);
    if (!strcmp(id,music_wanted[0])) memcpy(next,music_wanted[1],sizeof(next));
    xSemaphoreGive(music_lock);
    if (next[0]) m4_audio_tube_follow(next);
}

static bool music_needed(const char *id) {
    xSemaphoreTake(music_lock, portMAX_DELAY);
    bool needed = !strcmp(id, music_wanted[0]) || !strcmp(id, music_wanted[1]) ||
        (!download_stopped() && !strcmp(id, music_save_id));
    xSemaphoreGive(music_lock);
    return needed && !m4_preferences_restarting();
}
void m4_tube_music_prepare(const char *current, const char *next) {
    if (!music_lock || !video_id_valid(current)) return;
    xSemaphoreTake(music_lock, portMAX_DELAY);
    snprintf(music_wanted[0], sizeof(music_wanted[0]), "%s", current);
    snprintf(music_wanted[1], sizeof(music_wanted[1]), "%s", video_id_valid(next) && strcmp(current,next) ? next : "");
    for (unsigned i = 0; i < 2; ++i)
        if (music_slots[i].state == MUSIC_FAILED && !strcmp(music_slots[i].id,current)) memset(&music_slots[i],0,sizeof(music_slots[i]));
    xSemaphoreGive(music_lock);
}
void m4_tube_music_cache(const char *id, m4_tube_music_cache_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->message, sizeof(out->message), "Music waiting for PSRAM");
    if (!music_lock || !id) return;
    xSemaphoreTake(music_lock, portMAX_DELAY);
    for (unsigned i = 0; i < 2; ++i) if (!strcmp(music_slots[i].id,id)) {
        music_slot_t *slot = &music_slots[i];
        out->ready = slot->state == MUSIC_READY; out->loading = slot->state == MUSIC_LOADING;
        out->bytes = slot->bytes; out->total = slot->total;
        out->percent = slot->total ? (unsigned)((uint64_t)slot->bytes * 100 / slot->total) : 0;
        snprintf(out->message, sizeof(out->message), "%s", slot->message); break;
    }
    xSemaphoreGive(music_lock);
}
static music_slot_t *music_acquire(const char *id) {
    if (!music_lock) return NULL;
    music_slot_t *found = NULL;
    xSemaphoreTake(music_lock, portMAX_DELAY);
    for (unsigned i = 0; i < 2; ++i) if (music_slots[i].state == MUSIC_READY && !strcmp(music_slots[i].id,id)) {
        found = &music_slots[i]; ++found->refs; break;
    }
    xSemaphoreGive(music_lock); return found;
}
static void music_release(music_slot_t *slot) {
    xSemaphoreTake(music_lock, portMAX_DELAY); if (slot->refs) --slot->refs; xSemaphoreGive(music_lock);
}
static void music_slot_progress(music_slot_t *slot, const char *message, unsigned bytes, unsigned total) {
    xSemaphoreTake(music_lock, portMAX_DELAY);
    slot->bytes = bytes; slot->total = total; snprintf(slot->message,sizeof(slot->message),"%s",message);
    xSemaphoreGive(music_lock);
}
static void music_load(music_slot_t *slot) {
    const char *error = "Music cache download failed"; char detail[96] = "";
    bool owned = false, pc_ready = false; uint8_t *data = NULL, *buffer = NULL; unsigned total = 0, bytes = 0, duration = 0;
    esp_http_client_handle_t http = NULL;
    char url[320], job[33] = ""; sink_t sink = {0};
    if (network_get_mode() != NET_MODE_STA_CONNECTED) { error = "Connect Wi-Fi first"; goto done; }
    if (!lease_tls(1500,&owned)) { error = "HTTPS busy; stop radio or video and retry"; goto done; }
    buffer = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) { error = "Music PSRAM allocation failed"; goto done; }
    esp_http_client_config_t config = {.url=M4_TUBE_URL "/api/music",.timeout_ms=5000,.buffer_size=4096,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=sink_event,.user_data=&sink};
    http = esp_http_client_init(&config); if (!http) goto done;
    esp_http_client_set_header(http,"x-m3-token",M4_TUBE_TOKEN);
    esp_http_client_set_header(http,"Accept-Encoding","identity");
    esp_http_client_set_header(http,"Content-Length","0");
    snprintf(url,sizeof(url),"%s/api/music?id=%.11s",M4_TUBE_URL,slot->id);
    int status; int64_t admission_deadline=esp_timer_get_time()+30000000;
    do {
        status=download_json(http,&sink,buffer,url,HTTP_METHOD_POST);
        if (status!=409 || !music_needed(slot->id)) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    } while (esp_timer_get_time()<admission_deadline);
    if (status != 202) { error = status == 404 ? "Update PC relay for music cache" : status == 401 ?
        "Relay token rejected; rerun setup and rebuild" : "Music cache download failed"; goto done; }
    buffer[sink.length] = 0; cJSON *root = cJSON_Parse((char *)buffer);
    const cJSON *value = root ? cJSON_GetObjectItem(root,"job") : NULL;
    if (cJSON_IsString(value) && strlen(value->valuestring)==32 && strspn(value->valuestring,"0123456789abcdef")==32)
        snprintf(job,sizeof(job),"%s",value->valuestring);
    cJSON_Delete(root); if (!job[0]) goto done;
    xSemaphoreTake(music_lock,portMAX_DELAY); memcpy(slot->job,job,sizeof(job)); xSemaphoreGive(music_lock);
    esp_http_client_delete_header(http,"Content-Length");
    int64_t deadline = esp_timer_get_time() + 330LL * 1000000;
    bool ready = false;
    while (music_needed(slot->id) && esp_timer_get_time() < deadline) {
        snprintf(url,sizeof(url),"%s/api/music/status?job=%s",M4_TUBE_URL,job);
        status = download_json(http,&sink,buffer,url,HTTP_METHOD_GET); if (status != 200) goto done;
        buffer[sink.length]=0; root=cJSON_Parse((char *)buffer); value=root?cJSON_GetObjectItem(root,"state"):NULL;
        ready=cJSON_IsString(value) && !strcmp(value->valuestring,"ready");
        pc_ready=ready;
        bool failed=!cJSON_IsString(value) || (!ready && strcmp(value->valuestring,"preparing"));
        if (failed) { copy_text(detail,sizeof(detail),root?cJSON_GetObjectItem(root,"error"):NULL); }
        if (ready) {
            value=cJSON_GetObjectItem(root,"bytes"); total=cJSON_IsNumber(value) && value->valuedouble>0 && value->valuedouble<=10*1024*1024 ? (unsigned)value->valuedouble : 0;
            value=cJSON_GetObjectItem(root,"duration"); duration=cJSON_IsNumber(value) && value->valuedouble>0 && value->valuedouble<=1200 ? (unsigned)value->valuedouble : 0;
            copy_text(slot->title,sizeof(slot->title),cJSON_GetObjectItem(root,"title"));
            copy_text(slot->channel,sizeof(slot->channel),cJSON_GetObjectItem(root,"channel"));
            copy_text(slot->channel_id,sizeof(slot->channel_id),cJSON_GetObjectItem(root,"channel_id"));
        }
        cJSON_Delete(root);
        if (failed) { error=detail[0]?detail:"Music preparation failed"; goto done; }
        if (ready) break;
        for (unsigned i=0;i<5 && music_needed(slot->id);++i) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!ready || !music_needed(slot->id)) goto done;
    music_slot_progress(slot,"Loading music into PSRAM",0,total);
    if (total<128 || total>MUSIC_CACHE_BYTES || !duration) { error="Song exceeds PSRAM cache limit"; goto done; }
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < (size_t)total + MUSIC_CACHE_RESERVE) {
        error="Music PSRAM allocation failed"; goto done;
    }
    data=heap_caps_malloc(total,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) { error="Music PSRAM allocation failed"; goto done; }
    sink.data=NULL; esp_http_client_set_method(http,HTTP_METHOD_GET);
    snprintf(url,sizeof(url),"%s/api/music/file?job=%s",M4_TUBE_URL,job); esp_http_client_set_url(http,url);
    if (esp_http_client_open(http,0)!=ESP_OK || esp_http_client_fetch_headers(http)<0 || esp_http_client_get_status_code(http)!=200 ||
        esp_http_client_get_content_length(http)!=(int64_t)total) goto done;
    esp_http_client_set_timeout_ms(http,500);
    int64_t last=esp_timer_get_time();
    while (bytes<total && music_needed(slot->id)) {
        unsigned step=total-bytes; if (step>16384) step=16384;
        int got=esp_http_client_read(http,(char *)data+bytes,step);
        if (got<=0) {
            if ((got<0 && got!=-ESP_ERR_HTTP_EAGAIN) || esp_http_client_is_complete_data_received(http) || esp_timer_get_time()-last>30000000) break;
            vTaskDelay(pdMS_TO_TICKS(20)); continue;
        }
        bytes+=(unsigned)got; last=esp_timer_get_time(); music_slot_progress(slot,"Loading music into PSRAM",bytes,total);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (bytes!=total || !music_needed(slot->id)) goto done;
    xSemaphoreTake(music_lock,portMAX_DELAY);
    slot->data=data; slot->duration=duration; slot->state=MUSIC_READY; data=NULL;
    snprintf(slot->message,sizeof(slot->message),"Music ready in PSRAM");
    xSemaphoreGive(music_lock);
done:
    if (http) {
        esp_http_client_close(http);
        if (job[0] && !pc_ready && !music_needed(slot->id)) {
            snprintf(url,sizeof(url),"%s/api/music/status?job=%s",M4_TUBE_URL,job);
            download_json(http,&sink,buffer,url,HTTP_METHOD_DELETE);
        }
        esp_http_client_cleanup(http);
    }
    free(buffer); free(data); if (owned) https_memory_exclusive_end();
    xSemaphoreTake(music_lock,portMAX_DELAY);
    if (slot->state!=MUSIC_READY) { slot->state=MUSIC_FAILED; snprintf(slot->message,sizeof(slot->message),"%s",error); }
    xSemaphoreGive(music_lock);
}
static void music_save(const char *id, const char *title) {
    music_slot_t *slot=music_acquire(id); bool saved=false,locked=false,created=false; FILE *file=NULL;
    unsigned bytes=0,total=slot?slot->total:0;
    const char *error="Music cache download failed";
    char name[64],final[160],partial[168],job[33]="",url[320];
    uint8_t *buffer=NULL; esp_http_client_handle_t http=NULL; bool owned=false;
    snprintf(name,sizeof(name),"%.30s%s%.11s.mp3",title,title[0]?"_":"",id);
    snprintf(final,sizeof(final),SD_MEDIA_ROOT "/media/YouTube Music/%s",name); snprintf(partial,sizeof(partial),"%s.part",final);
    if (download_stopped()) { error="Download cancelled"; goto done; }
    if (!slot) {
        /* A long song or low PSRAM must still be downloadable to SD. */
        xSemaphoreTake(music_lock,portMAX_DELAY);
        for (unsigned i=0;i<2;++i) if (!strcmp(music_slots[i].id,id)) { memcpy(job,music_slots[i].job,sizeof(job)); total=music_slots[i].total; }
        xSemaphoreGive(music_lock);
        if (!job[0] || total<128 || total>10*1024*1024) goto done;
        if (network_get_mode()!=NET_MODE_STA_CONNECTED || !lease_tls(1500,&owned)) { error="Relay connection failed"; goto done; }
        buffer=heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); if (!buffer) { error="Music PSRAM allocation failed"; goto done; }
        snprintf(url,sizeof(url),"%s/api/music/file?job=%s",M4_TUBE_URL,job);
        esp_http_client_config_t config={.url=url,.timeout_ms=5000,.buffer_size=4096,.crt_bundle_attach=esp_crt_bundle_attach};
        http=esp_http_client_init(&config); if (!http) goto done;
        esp_http_client_set_header(http,"x-m3-token",M4_TUBE_TOKEN); esp_http_client_set_header(http,"Accept-Encoding","identity");
        if (esp_http_client_open(http,0)!=ESP_OK || esp_http_client_fetch_headers(http)<0 || esp_http_client_get_status_code(http)!=200 ||
            esp_http_client_get_content_length(http)!=(int64_t)total) goto done;
        esp_http_client_set_timeout_ms(http,500);
    }
    if (!(locked=sd_media_admin_lock())) { error="SD unavailable or busy"; goto done; }
    if ((mkdir(SD_MEDIA_ROOT "/media",0775)!=0 && errno!=EEXIST) ||
        (mkdir(SD_MEDIA_ROOT "/media/YouTube Music",0775)!=0 && errno!=EEXIST)) { error="SD music write failed"; goto done; }
    struct stat st; if (!stat(final,&st)) { saved=true; error="Music already saved on SD"; goto done; }
    uint64_t capacity,available;
    if (esp_vfs_fat_info(SD_MEDIA_ROOT,&capacity,&available)!=ESP_OK || available<(uint64_t)total+1024*1024) { error="Not enough SD space"; goto done; }
    unlink(partial); int fd=open(partial,O_WRONLY|O_CREAT|O_EXCL,0664);
    if (fd<0) { error="SD music write failed"; goto done; }
    created=true; file=fdopen(fd,"wb"); if (!file) { close(fd); error="SD music write failed"; goto done; }
    int64_t last=esp_timer_get_time();
    while (bytes<total && !download_stopped()) {
        unsigned step=total-bytes; if (step>16384) step=16384;
        const uint8_t *data=slot?slot->data+bytes:buffer;
        if (!slot) {
            int got=esp_http_client_read(http,(char *)buffer,step);
            if (got<=0) {
                if ((got<0 && got!=-ESP_ERR_HTTP_EAGAIN) || esp_http_client_is_complete_data_received(http) || esp_timer_get_time()-last>30000000) break;
                vTaskDelay(pdMS_TO_TICKS(20)); continue;
            }
            step=(unsigned)got; last=esp_timer_get_time();
        }
        if (fwrite(data,1,step,file)!=step) { error="SD music write failed"; goto done; }
        bytes+=step; download_progress("Saving music to SD",bytes,total); vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (download_stopped()) { error="Download cancelled"; goto done; }
    if (bytes!=total) { error="Download incomplete; retry"; goto done; }
    bool committed=fflush(file)==0 && fsync(fileno(file))==0;
    if (fclose(file)!=0) committed=false;
    file=NULL;
    if (!committed || rename(partial,final)!=0) { error="SD music write failed"; goto done; }
    created=false; saved=true; error="Music saved on SD";
done:
    if (file) fclose(file);
    if (created) unlink(partial);
    if (locked) { if (saved) sd_media_changed(); sd_media_admin_unlock(); }
    if (slot) music_release(slot);
    if (http) { esp_http_client_close(http); esp_http_client_cleanup(http); }
    free(buffer); if (owned) https_memory_exclusive_end();
    download_progress(error,bytes,total);
    xSemaphoreTake(music_lock,portMAX_DELAY); music_save_id[0]=0; xSemaphoreGive(music_lock);
    portENTER_CRITICAL(&service_lock); download_state.saved=saved;
    if (saved) snprintf(download_state.filename,sizeof(download_state.filename),"%s",name);
    download_state.busy=false; portEXIT_CRITICAL(&service_lock);
}
bool m4_tube_music_download(const char *id, const char *title) {
    if (!music_lock || !video_id_valid(id) || !sd_media_ready() || m4_preferences_restarting()) return false;
    portENTER_CRITICAL(&service_lock);
    if (download_state.busy) { portEXIT_CRITICAL(&service_lock); return false; }
    memset(&download_state,0,sizeof(download_state)); download_state.busy=download_state.music=true;
    snprintf(download_state.message,sizeof(download_state.message),"Preparing music on PC"); atomic_store(&download_cancel,false);
    portEXIT_CRITICAL(&service_lock);
    xSemaphoreTake(music_lock,portMAX_DELAY);
    snprintf(music_save_id,sizeof(music_save_id),"%s",id); m4_utf8_copy(music_save_title,sizeof(music_save_title),title?title:"");
    for (char *p=music_save_title;*p;++p) if ((unsigned char)*p<32 || *p==127 || strchr("\\/:*?\"<>|",*p) || (p==music_save_title && *p=='.')) *p='_';
    for (unsigned i=0;i<2;++i) if (music_slots[i].state==MUSIC_FAILED && !strcmp(music_slots[i].id,id)) memset(&music_slots[i],0,sizeof(music_slots[i]));
    xSemaphoreGive(music_lock); return true;
}
static void music_cache_worker(void *unused) {
    (void)unused;
    for (;;) {
        char ids[2][12],save[12],title[31]; music_slot_t *load=NULL; bool save_ready=false,save_failed=false;
        xSemaphoreTake(music_lock,portMAX_DELAY);
        memcpy(ids[0],music_wanted[0],12); memcpy(ids[1],music_save_id[0]?music_save_id:music_wanted[1],12);
        memcpy(save,music_save_id,12); memcpy(title,music_save_title,31);
        for (unsigned w=0;w<2 && !load && !m4_preferences_restarting();++w) {
            if (!ids[w][0]) continue;
            bool exists=false; for (unsigned i=0;i<2;++i) if (!strcmp(music_slots[i].id,ids[w])) exists=true;
            if (exists) continue;
            for (unsigned i=0;i<2;++i) {
                music_slot_t *s=&music_slots[i];
                if (s->refs || (s->id[0] && (!strcmp(s->id,ids[0]) || !strcmp(s->id,ids[1])))) continue;
                free(s->data); memset(s,0,sizeof(*s)); snprintf(s->id,sizeof(s->id),"%.11s",ids[w]);
                s->state=MUSIC_LOADING; snprintf(s->message,sizeof(s->message),"Preparing music on PC"); load=s; break;
            }
        }
        for (unsigned i=0;save[0] && i<2;++i) if (!strcmp(music_slots[i].id,save)) {
            save_ready=music_slots[i].state==MUSIC_READY; save_failed=music_slots[i].state==MUSIC_FAILED;
        }
        xSemaphoreGive(music_lock);
        if (load) music_load(load);
        if (save[0] && (save_ready || save_failed || download_stopped())) music_save(save,title);
        else if (!load) vTaskDelay(pdMS_TO_TICKS(100));
    }
}
static esp_err_t music_cache_start(void) {
    music_lock=xSemaphoreCreateMutex(); if (!music_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(music_cache_worker,"m4_music_cache",10240,NULL,3,NULL,0)==pdPASS) return ESP_OK;
    vSemaphoreDelete(music_lock); music_lock=NULL; return ESP_ERR_NO_MEM;
}
/* Returns false only before playback starts, permitting the legacy PCM fallback. */
static bool music_cached_run(const char *id,unsigned seconds,const m4_audio_output_t *out,bool *ended,char *error,size_t error_size) {
    if (!music_lock) return false;
    xSemaphoreTake(music_lock,portMAX_DELAY);
    if (strcmp(id,music_wanted[0])) { snprintf(music_wanted[0],sizeof(music_wanted[0]),"%s",id); music_wanted[1][0]=0; }
    xSemaphoreGive(music_lock);
    music_slot_t *slot=NULL; esp_audio_simple_dec_handle_t decoder=NULL; uint8_t *pcm=NULL;
    unsigned position=0,rate=0,stalled=0; uint64_t frames=0;
    bool opened=false,ok=false; int64_t deadline=esp_timer_get_time()+420LL*1000000;
    while (!out->cancelled() && esp_timer_get_time()<deadline && !(slot=music_acquire(id))) {
        m4_tube_music_cache_t state; m4_tube_music_cache(id,&state);
        if (!state.loading && strcmp(state.message,"Music waiting for PSRAM")) return false;
        player_update(true,state.percent,seconds*1000); out->message(state.message,true,state.percent);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!slot) return out->cancelled();
    snprintf(error,error_size,"Music decoder unavailable");
    pcm=heap_caps_malloc(MUSIC_DECODE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    esp_audio_simple_dec_cfg_t config={.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3};
    if (!pcm || !m4_audio_decoders_ready() || esp_audio_simple_dec_open(&config,&decoder)!=ESP_AUDIO_ERR_OK) goto done;
    portENTER_CRITICAL(&player_lock);
    player.duration_s=slot->duration; memcpy(player.title,slot->title,sizeof(player.title));
    memcpy(player.channel,slot->channel,sizeof(player.channel)); memcpy(player.channel_id,slot->channel_id,sizeof(player.channel_id));
    ++player.revision; portEXIT_CRITICAL(&player_lock);
    while (position<slot->total && !out->cancelled()) {
        while (atomic_load(&paused) && !out->cancelled()) vTaskDelay(pdMS_TO_TICKS(50));
        if (out->cancelled()) break;
        unsigned count=slot->total-position; if (count>4096) count=4096;
        esp_audio_simple_dec_raw_t raw={.buffer=slot->data+position,.len=count,.eos=position+count==slot->total};
        esp_audio_simple_dec_out_t frame={.buffer=pcm,.len=MUSIC_DECODE_BYTES};
        if (esp_audio_simple_dec_process(decoder,&raw,&frame)!=ESP_AUDIO_ERR_OK || raw.consumed>raw.len || frame.decoded_size>MUSIC_DECODE_BYTES) {
            snprintf(error,error_size,"Invalid music stream / decoder error"); goto done;
        }
        if (frame.decoded_size) {
            esp_audio_simple_dec_info_t info={0};
            if (esp_audio_simple_dec_get_info(decoder,&info)!=ESP_AUDIO_ERR_OK || info.bits_per_sample!=16 || info.channel!=2 || info.sample_rate!=32000) {
                snprintf(error,error_size,"Unsupported music PCM format"); goto done;
            }
            rate=info.sample_rate;
            if (frame.decoded_size%4) goto done;
            unsigned n=frame.decoded_size/4,skip=0; uint64_t target=(uint64_t)seconds*rate;
            if (frames<target) skip=target-frames<n?(unsigned)(target-frames):n;
            frames+=n;
            if (n>skip) {
                if (!opened) { if (!out->begin(rate)) { snprintf(error,error_size,"Music codec open failed"); goto done; } opened=true; }
                if (!out->write(pcm+skip*4,(n-skip)*4)) { snprintf(error,error_size,"Music I2S write failed"); goto done; }
            }
            unsigned ms=(unsigned)(frames*1000/rate); if (ms<seconds*1000) ms=seconds*1000;
            player_update(false,100,ms); out->message("Music playing from PSRAM",true,slot->duration?ms/10/slot->duration:0);
        }
        if (!raw.consumed) { if (!frame.decoded_size || ++stalled>8) { snprintf(error,error_size,"Music decoder stalled"); goto done; } }
        else { position+=raw.consumed; stalled=0; }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ok=frames && position==slot->total && (opened || seconds>=slot->duration) && !out->cancelled();
    if (ok && opened && !out->drain()) { ok=false; snprintf(error,error_size,"Music output drain failed"); }
done:
    if (decoder) esp_audio_simple_dec_close(decoder);
    free(pcm);
    music_release(slot);
    *ended=ok;
    return opened || ok || out->cancelled();
}
