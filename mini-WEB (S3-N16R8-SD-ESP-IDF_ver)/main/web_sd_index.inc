#include <errno.h>
/* All index state is protected by the SD storage mutex. */
#define SD_WEB_PAGE_SIZE 24
#define SD_INDEX_MAX 192
#define SD_INDEX_RESERVE (64U * 1024U)
typedef struct { char path[256]; uint64_t bytes; bool directory; } sd_index_entry_t;
static sd_index_entry_t *sd_index;
static unsigned sd_index_count,sd_index_capacity,sd_index_folder;
static uint32_t sd_index_revision;
static bool sd_index_complete,sd_index_failed;
static DIR *sd_index_dir;
static char sd_index_path[256];
static TickType_t sd_index_started;
static atomic_uint sd_web_connections;
static atomic_uint sd_web_last_close;
static TaskHandle_t sd_index_task_handle;
static esp_err_t sd_web_open(httpd_handle_t server,int socket) {
    (void)server;(void)socket;atomic_fetch_add(&sd_web_connections,1);return ESP_OK;
}
static void sd_web_close(httpd_handle_t server,int socket) {
    (void)server;close(socket);atomic_fetch_sub(&sd_web_connections,1);
    atomic_store(&sd_web_last_close,(unsigned)xTaskGetTickCount());
}
static bool sd_index_idle(void) {
    return web_server_is_running() && atomic_load(&sd_web_connections)==0 &&
        (TickType_t)(xTaskGetTickCount()-atomic_load(&sd_web_last_close))>=pdMS_TO_TICKS(10000);
}
static void sd_index_reset(void) {
    if(sd_index_dir) closedir(sd_index_dir);
    sd_index_dir=NULL;free(sd_index);sd_index=NULL;
    sd_index_count=sd_index_capacity=sd_index_folder=0;
    sd_index_complete=sd_index_failed=false;sd_index_path[0]=0;
    sd_index_revision=sd_media_revision();sd_index_started=xTaskGetTickCount();
}
static void sd_index_step(void) {
    if(sd_index_complete || sd_index_failed) return;
    if(!sd_index_dir) {
        char full[272];snprintf(full,sizeof(full),"%s%s",SD_MEDIA_ROOT,sd_index_path);
        sd_index_dir=opendir(full);
        if(!sd_index_dir) {sd_index_failed=true;return;}
    }
    errno=0;struct dirent *entry=readdir(sd_index_dir);
    if(!entry) {
        bool failed=errno!=0;closedir(sd_index_dir);sd_index_dir=NULL;
        if(failed) {sd_index_failed=true;return;}
        while(sd_index_folder<sd_index_count && !sd_index[sd_index_folder].directory) ++sd_index_folder;
        if(sd_index_folder==sd_index_count) {sd_index_complete=true;return;}
        snprintf(sd_index_path,sizeof(sd_index_path),"%s",sd_index[sd_index_folder++].path);return;
    }
    if(entry->d_name[0]=='.') return;
    char relative[256],full[272];
    int n=snprintf(relative,sizeof(relative),"%s/%s",sd_index_path,entry->d_name);
    if(n<0 || (size_t)n>=sizeof(relative)) return;
    snprintf(full,sizeof(full),"%s%s",SD_MEDIA_ROOT,relative);
    struct stat st;if(stat(full,&st)!=0) {sd_index_failed=true;return;}
    if(!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) return;
    if(sd_index_count==sd_index_capacity) {
        unsigned capacity=sd_index_capacity+8;
        size_t bytes=capacity*sizeof(*sd_index);
        if(capacity>SD_INDEX_MAX) {sd_index_failed=true;return;}
        /* realloc can temporarily require a second allocation. */
        if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<SD_INDEX_RESERVE+bytes ||
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<bytes) {sd_index_failed=true;return;}
        sd_index_entry_t *next=realloc(sd_index,bytes);
        if(!next) {sd_index_failed=true;return;}
        sd_index=next;sd_index_capacity=capacity;
    }
    sd_index_entry_t *item=&sd_index[sd_index_count++];
    snprintf(item->path,sizeof(item->path),"%s",relative);
    item->directory=S_ISDIR(st.st_mode);item->bytes=item->directory?0:(uint64_t)st.st_size;
}
static void sd_index_task(void *unused) {
    (void)unused;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(40));
        if(!sd_media_ready()) continue;
        bool idle=sd_index_idle();
        size_t free_bytes=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(!idle && free_bytes>=48U*1024U) continue;
        if(!sd_media_admin_lock()) continue;
        if(free_bytes<48U*1024U || sd_index_revision!=sd_media_revision() ||
           (TickType_t)(xTaskGetTickCount()-sd_index_started)>pdMS_TO_TICKS(60000)) sd_index_reset();
        if(idle && free_bytes>=SD_INDEX_RESERVE && sd_index_idle()) sd_index_step();
        if(sd_index_failed) {
            if(sd_index_dir) closedir(sd_index_dir);
            sd_index_dir=NULL;free(sd_index);sd_index=NULL;
            sd_index_count=sd_index_capacity=0;
        }
        sd_media_admin_unlock();
    }
}
static bool sd_index_child(const char *folder,const char *path,const char **name) {
    size_t length=!strcmp(folder,"/")?0:strlen(folder);
    if(strncmp(folder,path,length) || path[length]!='/') return false;
    *name=path+length+1;return **name && !strchr(*name,'/');
}
static esp_err_t sd_web_entry(httpd_req_t *req,const char *name,uint64_t bytes,bool directory,bool comma) {
    cJSON *item=cJSON_CreateObject();if(!item) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(item,"name",name);cJSON_AddNumberToObject(item,"size",(double)bytes);
    cJSON_AddBoolToObject(item,"directory",directory);
    char *json=cJSON_PrintUnformatted(item);cJSON_Delete(item);
    if(!json) return ESP_ERR_NO_MEM;
    esp_err_t result=comma?httpd_resp_sendstr_chunk(req,","):ESP_OK;
    if(result==ESP_OK) result=httpd_resp_sendstr_chunk(req,json);
    free(json);return result;
}
static esp_err_t sd_web_list(httpd_req_t *req,const char *folder,unsigned offset) {
    storage_capacity_t cap;storage_capacity_read(true,&cap);
    if(cap.error!=ESP_OK) return httpd_resp_send_500(req);
    bool cached=sd_index_complete && sd_index_revision==sd_media_revision();
    DIR *dir=NULL;char full[272];
    if(!cached) {
        snprintf(full,sizeof(full),"%s%s",SD_MEDIA_ROOT,!strcmp(folder,"/")?"":folder);
        dir=opendir(full);if(!dir) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Directory not found");
    } else if(strcmp(folder,"/")) {
        bool exists=false;
        for(unsigned i=0;i<sd_index_count;++i) if(sd_index[i].directory && !strcmp(sd_index[i].path,folder)) exists=true;
        if(!exists) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Directory not found");
    }
    httpd_resp_set_type(req,"application/json");
    esp_err_t result=httpd_resp_sendstr_chunk(req,"{\"files\":[");
    unsigned count=0,shown=0;
    if(cached) {
        for(unsigned i=0;result==ESP_OK && i<sd_index_count;++i) {
            const char *name;if(!sd_index_child(folder,sd_index[i].path,&name)) continue;
            if(count++<offset || shown==SD_WEB_PAGE_SIZE) continue;
            result=sd_web_entry(req,name,sd_index[i].bytes,sd_index[i].directory,shown++>0);
        }
    } else {
        struct dirent *entry;
        while(result==ESP_OK) {
            errno=0;entry=readdir(dir);
            if(!entry) {if(errno) result=ESP_FAIL;break;}
            if(entry->d_name[0]=='.') continue;
            char relative[256],target[272];
            int n=snprintf(relative,sizeof(relative),"%s/%s",!strcmp(folder,"/")?"":folder,entry->d_name);
            if(n<0 || (size_t)n>=sizeof(relative)) continue;
            snprintf(target,sizeof(target),"%s%s",SD_MEDIA_ROOT,relative);
            struct stat st;if(stat(target,&st)!=0) {result=ESP_FAIL;break;}
            if(!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) continue;
            if(count++<offset || shown==SD_WEB_PAGE_SIZE) continue;
            result=sd_web_entry(req,entry->d_name,S_ISDIR(st.st_mode)?0:(uint64_t)st.st_size,S_ISDIR(st.st_mode),shown++>0);
        }
        closedir(dir);
    }
    if(result!=ESP_OK) return result;
    char tail[256];snprintf(tail,sizeof(tail),"],\"ready\":true,\"cached\":%s,\"count\":%u,\"offset\":%u,\"page_size\":%u,\"total\":%.0f,\"used\":%.0f}",cached?"true":"false",count,offset,SD_WEB_PAGE_SIZE,(double)cap.total,(double)cap.used);
    result=httpd_resp_sendstr_chunk(req,tail);
    return result==ESP_OK?httpd_resp_sendstr_chunk(req,NULL):result;
}
