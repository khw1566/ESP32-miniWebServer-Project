/* The relay reads bounded chunks directly: no loopback socket and no HTTP
 * server task blocked while the remote client consumes a file. */
static const char *public_mime(const char *path) {
    const char *ext=strrchr(path,'.');
    static const char *types[][2]={
        {".html","text/html; charset=utf-8"},{".htm","text/html; charset=utf-8"},
        {".css","text/css; charset=utf-8"},{".js","application/javascript; charset=utf-8"},
        {".json","application/json"},{".svg","image/svg+xml"},{".png","image/png"},
        {".jpg","image/jpeg"},{".jpeg","image/jpeg"},{".gif","image/gif"},{".webp","image/webp"},
        {".ico","image/x-icon"},{".mp4","video/mp4"},{".webm","video/webm"},
        {".mp3","audio/mpeg"},{".wav","audio/wav"},{".m4a","audio/mp4"},{".pdf","application/pdf"},
        {".woff","font/woff"},{".woff2","font/woff2"},{".ttf","font/ttf"}
    };
    for(unsigned i=0;ext&&i<sizeof(types)/sizeof(types[0]);++i) if(!strcasecmp(ext,types[i][0])) return types[i][1];
    return "text/plain; charset=utf-8";
}
/* Shown through the domain for a missing public file. */
static const char PUBLIC_NOT_FOUND[]="<!doctype html><html lang='ko'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>페이지를 찾을 수 없음</title><style>:root{color-scheme:light dark;--bg:#f5f7fa;--card:#fff;--text:#1f2a37;--muted:#5b6b7b;--line:#dde3ea;--accent:#2563eb}@media(prefers-color-scheme:dark){:root{--bg:#0f141b;--card:#18212c;--text:#e6edf3;--muted:#9aa8b6;--line:#2a3644;--accent:#78a9ff}}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:24px;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Malgun Gothic',sans-serif;line-height:1.7;word-break:keep-all}.card{max-width:440px;width:100%;background:var(--card);border:1px solid var(--line);border-radius:16px;padding:32px 28px;text-align:center}.mark{width:48px;height:48px;margin:0 auto 16px;border-radius:12px;background:var(--accent);opacity:.9}h1{font-size:22px;margin:0 0 8px}p{margin:0;color:var(--muted);font-size:15px}</style></head><body><main class='card'><div class='mark' aria-hidden='true'></div><h1>페이지를 찾을 수 없습니다</h1><p>주소가 바뀌었거나 삭제된 파일입니다.<br><a href='/' style='color:var(--accent)'>첫 화면으로 이동</a></p></main></body></html>";
static void public_stream_message(web_public_stream_t *s,int status,const char *message) {
    if(s->file) fclose(s->file);
    s->file=NULL;s->body=message;s->length=strlen(message);s->offset=0;
    s->status=status;s->mime="text/plain; charset=utf-8";
}
bool web_server_public_open(const char *path,const char *range,web_public_stream_t *s) {
    memset(s,0,sizeof(*s));s->status=200;
    if(!cloud_relay_public_path(path)) return false;
    if(!web_server_is_running() || network_get_mode()!=NET_MODE_STA_CONNECTED) {
        public_stream_message(s,503,"Board is not connected to Wi-Fi");return true;
    }
    if(!strcmp(path,"/media/catalog.json")) {
        cJSON *catalog=sd_media_catalog();s->owned_body=catalog?cJSON_PrintUnformatted(catalog):NULL;cJSON_Delete(catalog);
        if(!s->owned_body) public_stream_message(s,503,"Cannot read SD catalog");
        else {s->body=s->owned_body;s->length=strlen(s->body);s->mime="application/json";}
        return true;
    }
    if(!strcmp(path,"/")) __atomic_fetch_add(&s_home_requests,1,__ATOMIC_RELAXED);
    char filename[280];struct stat st;
    if(sd_media_ready() && (strncmp(path,"/media/",7) || sd_media_name_valid(path+7))) {
        snprintf(filename,sizeof(filename),"%s%s",SD_MEDIA_ROOT,!strcmp(path,"/")?"/index.html":path);
        if(stat(filename,&st)==0 && S_ISREG(st.st_mode)) s->file=fopen(filename,"rb");
    }
    if(!s->file) {
        if(!strcmp(path,"/")) {
            char initial[128]={0};
            bool selected=get_initial_page(initial,sizeof(initial)) && is_valid_initial_page(initial);
            snprintf(filename,sizeof(filename),"%s%s",STORAGE_BASE_PATH,selected?initial:"/index.html");
            if(selected && (stat(filename,&st)!=0 || !S_ISREG(st.st_mode))) snprintf(filename,sizeof(filename),STORAGE_BASE_PATH "/index.html");
        } else snprintf(filename,sizeof(filename),"%s%s",STORAGE_BASE_PATH,path);
        if(stat(filename,&st)==0 && S_ISREG(st.st_mode)) s->file=fopen(filename,"rb");
    }
    if(!s->file) {
        /* Nothing published yet: the same built-in page as the local home
         * (showcase when selected, otherwise the start guide). The admin
         * redirect choice is not public, so it also gets the guide. */
        bool home=!strcmp(path,"/") || !strcmp(path,"/index.html");
        if(home) public_stream_message(s,200,get_initial_view_mode()==1?HTML_MAIN:HTML_HELP_GUIDE);
        else public_stream_message(s,404,PUBLIC_NOT_FOUND);
        s->mime="text/html; charset=utf-8";return true;
    }
    if(fseek(s->file,0,SEEK_END)!=0) {public_stream_message(s,503,"Cannot read file");return true;}
    long size=ftell(s->file),start=0,end=size-1;
    if(size<0) {public_stream_message(s,503,"Cannot read file size");return true;}
    s->mime=public_mime(filename);
    if(range && *range) {
        char *tail;const char *p=range;
        if(strncmp(p,"bytes=",6) || strchr(p,',')) goto invalid_range;
        p+=6;
        if(*p=='-') {
            ++p;if(*p<'0'||*p>'9') goto invalid_range;
            long suffix=strtol(p,&tail,10);if(*tail || suffix<=0) goto invalid_range;
            start=suffix>=size?0:size-suffix;
        } else {
            if(*p<'0'||*p>'9') goto invalid_range;
            start=strtol(p,&tail,10);if(*tail!='-') goto invalid_range;p=tail+1;
            if(*p) {if(*p<'0'||*p>'9') goto invalid_range;end=strtol(p,&tail,10);if(*tail) goto invalid_range;}
        }
        if(size==0 || start<0 || start>=size || end<start) goto invalid_range;
        if(end>=size) end=size-1;
        s->status=206;snprintf(s->content_range,sizeof(s->content_range),"bytes %ld-%ld/%ld",start,end,size);
    }
    s->length=size?(size_t)(end-start+1):0;
    if(s->length>8U*1024U*1024U) {s->content_range[0]=0;public_stream_message(s,503,"File exceeds relay transfer limit");return true;}
    if(fseek(s->file,start,SEEK_SET)!=0) {s->content_range[0]=0;public_stream_message(s,503,"Cannot seek file");}
    return true;
invalid_range:
    snprintf(s->content_range,sizeof(s->content_range),"bytes */%ld",size);
    public_stream_message(s,416,"");return true;
}
size_t web_server_public_read(web_public_stream_t *s,void *buffer,size_t length) {
    if(length>s->length-s->offset) length=s->length-s->offset;
    size_t count=0;
    if(s->file) count=fread(buffer,1,length,s->file);
    else if(length) {memcpy(buffer,s->body+s->offset,length);count=length;}
    s->offset+=count;return count;
}
void web_server_public_close(web_public_stream_t *s) {
    if(s->file) fclose(s->file);
    free(s->owned_body);memset(s,0,sizeof(*s));
}
