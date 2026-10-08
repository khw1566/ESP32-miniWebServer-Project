#include "sd_media.h"
#include "board_display.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <stdatomic.h>
static atomic_uint sd_revision;
uint32_t sd_media_revision(void) { return atomic_load(&sd_revision); }
void sd_media_changed(void) { atomic_fetch_add(&sd_revision,1); }
static sdmmc_card_t *card;
static esp_err_t mount_error = ESP_ERR_INVALID_STATE;
static const char *mount_stage = "not_initialized";
static esp_err_t mount_failed(esp_err_t error, const char *stage) {
    mount_error = error;
    mount_stage = stage;
    ESP_LOGW("SD_MEDIA", "%s: %s", stage, esp_err_to_name(error));
    return error;
}
static SemaphoreHandle_t lock;
static FILE *upload;
static char session[40], filename[65], expected_hash[65];
static uint32_t expected_size, written;
static int64_t last_activity;
static mbedtls_sha256_context digest;
static const char *temporary = SD_MEDIA_ROOT "/media/.upload.tmp";
bool sd_media_ready(void) { return card != NULL; }
bool sd_media_name_valid(const char *name) {
    if (!name || !name[0] || strlen(name) > 64 || name[0] == '.' || strstr(name,"..")) return false;
    for (const char *p=name;*p;++p)
        if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("_-.",*p))) return false;
    const char *ext=strrchr(name,'.');
    const char *types[]={".jpg",".jpeg",".png",".webp",".gif",".mp4",".webm",".mp3",".wav",".m4a"};
    for(unsigned i=0;ext&&i<sizeof(types)/sizeof(types[0]);++i) if(!strcasecmp(ext,types[i])) return true;
    return false;
}
static void abort_upload(void) {
    if(upload) fclose(upload);
    upload=NULL;session[0]=0;written=0;
    mbedtls_sha256_free(&digest);unlink(temporary);
}
bool sd_media_admin_lock(void) {
    if (!card || !lock || xSemaphoreTake(lock, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    if (upload && esp_timer_get_time()-last_activity>300LL*1000000) abort_upload();
    if (upload) { xSemaphoreGive(lock); return false; }
    return true;
}
void sd_media_admin_unlock(void) { xSemaphoreGive(lock); }
esp_err_t sd_media_init(void) {
    if(card) return ESP_OK;
    if(!lock) lock=xSemaphoreCreateMutex();
    if(!lock) return mount_failed(ESP_ERR_NO_MEM, "mutex");
    sdmmc_host_t host=SDSPI_HOST_DEFAULT();host.slot=SPI3_HOST;host.max_freq_khz=10000;
    spi_bus_config_t bus={.mosi_io_num=M1_SD_MOSI,.miso_io_num=M1_SD_MISO,.sclk_io_num=M1_SD_CLK,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=4096};
    esp_err_t err=spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO);
    if(err!=ESP_OK) return mount_failed(err, "spi_bus");
    sdspi_device_config_t slot=SDSPI_DEVICE_CONFIG_DEFAULT();slot.host_id=SPI3_HOST;slot.gpio_cs=M1_SD_CS;
    esp_vfs_fat_mount_config_t mount={.format_if_mount_failed=false,.max_files=6,.allocation_unit_size=16*1024};
    err=esp_vfs_fat_sdspi_mount(SD_MEDIA_ROOT,&host,&slot,&mount,&card);
    if(err!=ESP_OK) {card=NULL;spi_bus_free(SPI3_HOST);return mount_failed(err, "card_or_filesystem");}
    if(mkdir(SD_MEDIA_ROOT "/media",0775)!=0&&errno!=EEXIST) {
        esp_vfs_fat_sdcard_unmount(SD_MEDIA_ROOT,card);card=NULL;spi_bus_free(SPI3_HOST);return mount_failed(ESP_FAIL, "media_directory");
    }
    unlink(temporary);
    unlink(SD_MEDIA_ROOT "/.admin-upload.tmp");
    mount_error = ESP_OK;
    mount_stage = "ready";
    ESP_LOGI("SD_MEDIA","SD media storage ready");return ESP_OK;
}
cJSON *sd_media_catalog(void) {
    cJSON *root=cJSON_CreateObject(),*files=cJSON_CreateArray();
    if(!root||!files) {cJSON_Delete(root);cJSON_Delete(files);return NULL;}
    cJSON_AddBoolToObject(root,"ready",sd_media_ready());cJSON_AddItemToObject(root,"files",files);
    cJSON_AddStringToObject(root,"stage",mount_stage);
    cJSON_AddStringToObject(root,"error",esp_err_to_name(mount_error));
#if defined(CONFIG_FATFS_LFN_HEAP) || defined(CONFIG_FATFS_LFN_STACK)
    cJSON_AddBoolToObject(root,"long_filenames",true);
#else
    cJSON_AddBoolToObject(root,"long_filenames",false);
#endif
    if(!card||xSemaphoreTake(lock,pdMS_TO_TICKS(1000))!=pdTRUE) return root;
    DIR *dir=opendir(SD_MEDIA_ROOT "/media");struct dirent *entry;unsigned count=0;
    while(dir&&(entry=readdir(dir))&&count<64) {
        if(!sd_media_name_valid(entry->d_name)) continue;
        char path[128];
        snprintf(path,sizeof(path),SD_MEDIA_ROOT "/media/%.64s",entry->d_name);
        struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode)) continue;
        cJSON *file=cJSON_CreateObject();if(!file) break;
        cJSON_AddStringToObject(file,"name",entry->d_name);cJSON_AddNumberToObject(file,"size",st.st_size);
        cJSON_AddItemToArray(files,file);count++;
    }
    if(dir) {
        closedir(dir);
    }
    xSemaphoreGive(lock);
    return root;
}
static const char *text(const cJSON *obj,const char *key) {
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(obj,key);return cJSON_IsString(item)?item->valuestring:"";
}
static bool hex64(const char *s) {
    if(strlen(s)!=64) return false;
    for(const char *p=s;*p;++p) if(!strchr("0123456789abcdef",*p)) return false;
    return true;
}
static cJSON *answer(int status,const char *message) {
    cJSON *out=cJSON_CreateObject();if(!out) return NULL;
    cJSON_AddNumberToObject(out,"status",status);cJSON_AddStringToObject(out,"message",message);
    cJSON_AddNumberToObject(out,"offset",written);return out;
}
cJSON *sd_media_command(const cJSON *cmd) {
    if(!card) return answer(503,"SD card is not mounted");
    if(xSemaphoreTake(lock,pdMS_TO_TICKS(3000))!=pdTRUE) return answer(503,"Storage is busy");
    int status=400;const char *message="Invalid upload command";
    const char *action=text(cmd,"action"),*sid=text(cmd,"session");
    if(upload&&esp_timer_get_time()-last_activity>300LL*1000000) abort_upload();
    if(!strcmp(action,"begin")) {
        const char *name=text(cmd,"name"),*hash=text(cmd,"sha256");
        const cJSON *size=cJSON_GetObjectItemCaseSensitive(cmd,"size");
        if(upload) {status=409;message="Another upload is active";goto done;}
        if(strlen(sid)!=36||!sd_media_name_valid(name)||!hex64(hash)||!cJSON_IsNumber(size)||
            size->valuedouble<1||size->valuedouble>SD_MEDIA_MAX_BYTES||size->valuedouble!=(uint32_t)size->valuedouble) goto done;
        for(const char *p=sid;*p;++p) if(!strchr("0123456789abcdef-",*p)) goto done;
        char target[128];snprintf(target,sizeof(target),SD_MEDIA_ROOT "/media/%.64s",name);
        struct stat st;if(stat(target,&st)==0) {status=409;message="Filename already exists";goto done;}
        upload=fopen(temporary,"wb");if(!upload) {status=507;message="Cannot write SD card";goto done;}
        snprintf(session,sizeof(session),"%s",sid);snprintf(filename,sizeof(filename),"%s",name);
        snprintf(expected_hash,sizeof(expected_hash),"%s",hash);expected_size=(uint32_t)size->valuedouble;written=0;
        mbedtls_sha256_init(&digest);
        if(mbedtls_sha256_starts(&digest,0)!=0) {abort_upload();status=500;message="Digest initialization failed";goto done;}
        status=200;message="Upload started";
    } else {
        if(!upload||strcmp(session,sid)) {status=409;message="Upload session expired";goto done;}
        if(!strcmp(action,"abort")) {abort_upload();status=200;message="Upload cancelled";goto done;}
        if(!strcmp(action,"chunk")) {
            const cJSON *offset=cJSON_GetObjectItemCaseSensitive(cmd,"offset");
            unsigned char buffer[2048];size_t count=0;const char *data=text(cmd,"data");
            if(!cJSON_IsNumber(offset)||offset->valuedouble!=written) {status=409;message="Chunk offset mismatch";goto done;}
            if(strlen(data)>2732||mbedtls_base64_decode(buffer,sizeof(buffer),&count,(const unsigned char*)data,strlen(data))!=0||
                count==0||written+count>expected_size) goto done;
            if(fwrite(buffer,1,count,upload)!=count||fflush(upload)!=0||mbedtls_sha256_update(&digest,buffer,count)!=0) {
                abort_upload();status=507;message="SD write failed";goto done;
            }
            written+=count;status=200;message="Chunk saved";
        } else if(!strcmp(action,"commit")) {
            if(written!=expected_size) {status=409;message="Upload is incomplete";goto done;}
            unsigned char hash[32];char encoded[65];
            if(mbedtls_sha256_finish(&digest,hash)!=0) {abort_upload();status=500;message="Digest failed";goto done;}
            for(unsigned i=0;i<32;++i) snprintf(encoded+i*2,3,"%02x",hash[i]);
            if(strcmp(encoded,expected_hash)) {abort_upload();status=422;message="File checksum mismatch";goto done;}
            bool flushed=fflush(upload)==0&&fsync(fileno(upload))==0;
            int closed=fclose(upload);upload=NULL;mbedtls_sha256_free(&digest);
            char target[128];snprintf(target,sizeof(target),SD_MEDIA_ROOT "/media/%.64s",filename);
            struct stat st;
            if(!flushed||closed!=0||stat(target,&st)==0||rename(temporary,target)!=0) {
                unlink(temporary);session[0]=0;status=507;message="Cannot publish SD file";goto done;
            }
            sd_media_changed();session[0]=0;status=200;message="Upload complete";
        }
    }
    last_activity=esp_timer_get_time();
done:;
    cJSON *out=answer(status,message);xSemaphoreGive(lock);return out;
}
