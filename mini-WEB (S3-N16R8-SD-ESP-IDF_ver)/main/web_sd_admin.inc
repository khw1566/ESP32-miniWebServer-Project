#include "storage_view.h"
#include "web_sd_index.inc"
/* Local administrator API. Public relay paths exclude /api/. */
static bool sd_admin_auth(httpd_req_t *req) {
    char password[64] = {0};
    /* A typed password wins; otherwise the browser's session cookie. */
    bool typed = httpd_req_get_hdr_value_str(req,"X-Admin-Password",password,sizeof(password)) == ESP_OK && password[0];
    bool ok = typed ? verify_admin_password(password) : ai_session_for(req) != NULL;
    memset(password,0,sizeof(password));
    if (!ok) httpd_resp_send_err(req,HTTPD_401_UNAUTHORIZED,"Administrator authentication required");
    return ok;
}
static bool sd_admin_path(httpd_req_t *req, char *path, size_t capacity) {
    char query[896],encoded[800];
    if (httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
        httpd_query_key_value(query,"path",encoded,sizeof(encoded))!=ESP_OK ||
        !decode_wifi_field(path,capacity,encoded) || path[0]!='/' || strstr(path,"..") || strstr(path,"//")) return false;
    for (const unsigned char *p=(const unsigned char *)path;*p;++p) {
        if (*p<32 || *p==127 || *p=='\\' || *p==':' || (*p=='/' && p[1]=='.')) return false;
    }
    return true;
}
static esp_err_t sd_admin_error(httpd_req_t *req, const char *status, const char *message) {
    httpd_resp_set_status(req,status);
    return httpd_resp_sendstr(req,message);
}
static esp_err_t sd_admin_get(httpd_req_t *req) {
    if (!sd_admin_auth(req)) return ESP_OK;
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char path[256],query[896],action[16]={0};
    if (!sd_admin_path(req,path,sizeof(path))) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid SD path");
    if (!sd_media_ready()) return sd_admin_error(req,"503 Service Unavailable","SD card is not mounted");
    if (!sd_media_admin_lock()) return sd_admin_error(req,"409 Conflict","SD storage is busy");
    httpd_req_get_url_query_str(req,query,sizeof(query));
    httpd_query_key_value(query,"action",action,sizeof(action));
    esp_err_t sent=ESP_OK;
    if (!strcmp(action,"download") || !strcmp(action,"preview")) {
        char full[272];snprintf(full,sizeof(full),"%s%s",SD_MEDIA_ROOT,path);
        struct stat st;FILE *file=NULL;
        if (stat(full,&st)!=0 || !S_ISREG(st.st_mode) || !(file=fopen(full,"rb"))) {
            sd_media_admin_unlock();return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"File not found");
        }
        if(!strcmp(action,"preview") && st.st_size>1024*1024) {fclose(file);sd_media_admin_unlock();return sd_admin_error(req,"413 Content Too Large","Preview is limited to 1 MiB");}
        httpd_resp_set_type(req,"application/octet-stream");
        httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
        httpd_resp_set_hdr(req,"Content-Disposition","attachment");
        char buffer[1024];size_t count;
        while ((count=fread(buffer,1,sizeof(buffer),file))>0 && sent==ESP_OK) sent=httpd_resp_send_chunk(req,buffer,count);
        bool failed=ferror(file);fclose(file);
        if (failed) sent=ESP_FAIL;
        if (sent==ESP_OK) sent=httpd_resp_sendstr_chunk(req,NULL);
    } else {
        char offset_text[16]={0};unsigned offset=0;
        if (httpd_query_key_value(query,"offset",offset_text,sizeof(offset_text))==ESP_OK) {
            char *end;unsigned long value=strtoul(offset_text,&end,10);
            if (!offset_text[0] || *end || value>100000) {sd_media_admin_unlock();return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid offset");}
            offset=(unsigned)value;
        }
        sent=sd_web_list(req,path,offset);
    }
    sd_media_admin_unlock();return sent;
}
static esp_err_t sd_admin_upload(httpd_req_t *req) {
    if (!sd_admin_auth(req)) return ESP_OK;
    char path[256];
    if (!sd_admin_path(req,path,sizeof(path)) || !strcmp(path,"/") || path[strlen(path)-1]=='/') return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid filename");
    if (req->content_len<1 || req->content_len>SD_MEDIA_MAX_BYTES) return sd_admin_error(req,"413 Content Too Large","File must be between 1 byte and 8 MiB");
    if (!sd_media_ready()) return sd_admin_error(req,"503 Service Unavailable","SD card is not mounted");
    if (!sd_media_admin_lock()) return sd_admin_error(req,"409 Conflict","SD storage is busy");
    char full[272];snprintf(full,sizeof(full),"%s%s",SD_MEDIA_ROOT,path);
    const char *temp=SD_MEDIA_ROOT "/.admin-upload.tmp";struct stat st;
    if (stat(full,&st)==0) {sd_media_admin_unlock();return sd_admin_error(req,"409 Conflict","Filename already exists; delete it first or rename the upload");}
    storage_capacity_t cap;storage_capacity_read(true,&cap);
    if(cap.error!=ESP_OK || cap.total-cap.used<(uint64_t)req->content_len+16384) {sd_media_admin_unlock();return sd_admin_error(req,"507 Insufficient Storage","Not enough SD space");}
    FILE *file=fopen(temp,"wb");
    if(!file) {sd_media_admin_unlock();return sd_admin_error(req,"507 Insufficient Storage","Cannot write SD card");}
    int remaining=req->content_len;unsigned timeouts=0;bool ok=true;char buffer[1024];
    while(remaining>0) {
        int count=httpd_req_recv(req,buffer,MIN(remaining,sizeof(buffer)));
        if(count==HTTPD_SOCK_ERR_TIMEOUT && ++timeouts<3) continue;
        if(count<=0 || fwrite(buffer,1,count,file)!=(size_t)count) {ok=false;break;}
        timeouts=0;remaining-=count;
    }
    if(fflush(file)!=0 || fsync(fileno(file))!=0) ok=false;
    if(fclose(file)!=0) ok=false;
    if(ok && rename(temp,full)!=0) ok=false;
    if(!ok) unlink(temp);
    if(ok) sd_media_changed();
    sd_media_admin_unlock();
    if(!ok) return sd_admin_error(req,"500 Internal Server Error","SD upload failed; incomplete file removed");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"status\":\"ok\"}");
}
static esp_err_t sd_admin_delete(httpd_req_t *req) {
    if(!sd_admin_auth(req)) return ESP_OK;
    char path[256];
    if(!sd_admin_path(req,path,sizeof(path)) || !strcmp(path,"/")) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid file path");
    if(!sd_media_ready()) return sd_admin_error(req,"503 Service Unavailable","SD card is not mounted");
    if(!sd_media_admin_lock()) return sd_admin_error(req,"409 Conflict","SD storage is busy");
    char full[272];snprintf(full,sizeof(full),"%s%s",SD_MEDIA_ROOT,path);struct stat st;
    bool exists=stat(full,&st)==0 && S_ISREG(st.st_mode);bool ok=exists && unlink(full)==0;
    if(ok) sd_media_changed();
    sd_media_admin_unlock();
    if(!exists) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"File not found");
    if(!ok) return httpd_resp_send_500(req);
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"status\":\"ok\"}");
}
