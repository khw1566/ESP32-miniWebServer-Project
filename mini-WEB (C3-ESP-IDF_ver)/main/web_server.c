#include "web_server.h"
#include "cloud_relay.h"
#include "https_memory.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#include <stdatomic.h>
#include <time.h>
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qrcode.h"
#include "web_pages.h"
#include "cJSON.h"
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>


static const char *TAG = "WEB_SVR";
static httpd_handle_t s_server = NULL;
static atomic_bool s_server_running;
bool web_server_is_running(void) { return atomic_load(&s_server_running); }
static uint32_t s_home_requests;
uint32_t web_server_home_requests(void) {return __atomic_load_n(&s_home_requests,__ATOMIC_RELAXED);}
#define STORAGE_BASE_PATH "/spiffs"
static void agent_recover_files(void);

static void web_factory_seed(bool force);
esp_err_t web_server_storage_init(void) {
  esp_vfs_spiffs_conf_t conf = {
      .base_path = STORAGE_BASE_PATH,
      .partition_label =
          "littlefs", // Storage capacity comes from the board partition table.
      .max_files = 10,
      .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    }
    return ret;
  }

  agent_recover_files();
  size_t total = 0, used = 0;
  ret = esp_spiffs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "SPIFFS Partition size: total: %d KB, used: %d KB",
             (int)(total / 1024), (int)(used / 1024));
  }
  web_factory_seed(false);
  return ESP_OK;
}

#define LOG_BUFFER_SIZE 4096
static char s_log_buffer[LOG_BUFFER_SIZE];
static size_t s_log_head = 0;
static SemaphoreHandle_t s_log_mutex = NULL;
static vprintf_like_t s_default_vprintf = NULL;

static int web_log_vprintf(const char *fmt, va_list args) {
  int printed = 0;
  if (s_default_vprintf) {
    va_list args_copy;
    va_copy(args_copy, args);
    printed = s_default_vprintf(fmt, args_copy);
    va_end(args_copy);
  }

  // Driver logging must not wait on a lock owned by the HTTP task.
  // In particular, blocking queue calls assert while the scheduler is suspended.
  if (s_log_mutex && xSemaphoreTake(s_log_mutex, 0) == pdTRUE) {
    char temp[256];
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    if (len > 0) {
      // vsnprintf returns the required length, which may exceed temp's size.
      // Never copy beyond the bytes actually present in temp.
      size_t copy_len = MIN((size_t)len, sizeof(temp) - 1);
      if (s_log_head + copy_len >= LOG_BUFFER_SIZE) {
        size_t keep = LOG_BUFFER_SIZE / 2;
        size_t retained = MIN(s_log_head, keep);
        memmove(s_log_buffer, s_log_buffer + (s_log_head - retained), retained);
        s_log_head = retained;
      }
      memcpy(s_log_buffer + s_log_head, temp, copy_len);
      s_log_head += copy_len;
      s_log_buffer[s_log_head] = '\0';
    }
    xSemaphoreGive(s_log_mutex);
  }
  return printed;
}

esp_err_t web_server_log_init(void) {
  if (!s_log_mutex) {
    s_log_mutex = xSemaphoreCreateMutex();
    s_log_buffer[0] = '\0';
    s_default_vprintf = esp_log_set_vprintf(web_log_vprintf);
    ESP_LOGI(TAG, "Log Ring-Buffer initialized (%d KB)",
             LOG_BUFFER_SIZE / 1024);
  }
  return ESP_OK;
}

#define NVS_SETTINGS_NAMESPACE "web_cfg"
#define DEFAULT_ADMIN_PW "00000000"

#include "web_ai_security.inc"

/* First boot: the product home page becomes the editable /spiffs/index.html.
 * Deleting or replacing it is remembered (NVS flag); a storage reset brings
 * it back. Generated from web_examples/00_product_home by tools/embed_factory_page.cjs. */
static void web_factory_seed(bool force) {
  nvs_handle_t h;
  uint8_t seeded = 0;
  if (!force && nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "factory_seed", &seeded);
    nvs_close(h);
  }
  if (seeded) return;
  const char *path = STORAGE_BASE_PATH "/index.html";
  struct stat st;
  if (force || stat(path, &st) != 0) {
    FILE *f = fopen(path, "wb");
    size_t written = f ? fwrite(FACTORY_INDEX_HTML, 1, FACTORY_INDEX_HTML_LEN, f) : 0;
    if (f) fclose(f);
    if (written != FACTORY_INDEX_HTML_LEN) {
      unlink(path);
      ESP_LOGW(TAG, "Factory home page could not be written");
      return;
    }
    ESP_LOGI(TAG, "Factory home page installed (%u bytes)", (unsigned)written);
  }
  if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, "factory_seed", 1);
    nvs_commit(h);
    nvs_close(h);
  }
}

static int get_initial_view_mode(void) {
  int mode = 0; // 0: 도움말/가이드 화면(기본값), 1: 메인 쇼케이스(HTML_MAIN),
                // 2: 관리자 대시보드(/admin)
  nvs_handle_t h;
  if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
    int32_t val = 0;
    if (nvs_get_i32(h, "init_view", &val) == ESP_OK) {
      mode = (int)val;
    }
    nvs_close(h);
  }
  return mode;
}

static esp_err_t set_initial_view_mode(int mode) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_i32(h, "init_view", (int32_t)mode);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  return err;
}

static bool get_initial_page(char *out_page, size_t page_len) {
  if (!out_page || page_len == 0) return false;
  out_page[0] = '\0';

  nvs_handle_t h;
  if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  size_t stored_len = page_len;
  esp_err_t err = nvs_get_str(h, "init_page", out_page, &stored_len);
  nvs_close(h);
  if (err != ESP_OK || out_page[0] != '/') {
    out_page[0] = '\0';
    return false;
  }
  return true;
}

static esp_err_t set_initial_page(const char *page) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  if (!page || page[0] == '\0') {
    err = nvs_erase_key(h, "init_page");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  } else {
    err = nvs_set_str(h, "init_page", page);
  }
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err;
}

static bool is_valid_initial_page(const char *page) {
  if (!page || page[0] != '/' || strlen(page) < 6 || strlen(page) >= 128 ||
      strstr(page, "..") != NULL) {
    return false;
  }

  for (const char *p = page; *p; p++) {
    if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '/' || *p == '-' ||
           *p == '_' || *p == '.')) {
      return false;
    }
  }

  size_t len = strlen(page);
  bool is_html = (len >= 5 && strcasecmp(page + len - 5, ".html") == 0) ||
                 (len >= 4 && strcasecmp(page + len - 4, ".htm") == 0);
  if (!is_html) {
    return false;
  }

  char filepath[sizeof(STORAGE_BASE_PATH) + 128];
  if (snprintf(filepath, sizeof(filepath), "%s%s", STORAGE_BASE_PATH, page) >=
      sizeof(filepath)) {
    return false;
  }
  FILE *file = fopen(filepath, "rb");
  if (!file) return false;
  fclose(file);
  return true;
}

#include "web_file_dates.inc"
#include "web_agent_tools.inc"
#include "web_ai_proxy.inc"
#include "web_ai_login.inc"

static esp_err_t send_chunk_string(httpd_req_t *req, const char *str);
#include "web_wifi_setup_html.inc"
static esp_err_t render_setup_page(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return send_chunk_string(req, HTML_WIFI_SETUP);
}

#include "web_media_range.inc"

static esp_err_t send_spiffs_file(httpd_req_t *req, const char *filepath) {
  FILE *f = fopen(filepath, "rb");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }

  // MIME 타입 판별
  const char *ext = strrchr(filepath, '.');
  if (ext && strcasecmp(ext, ".mp3") == 0) return send_media_file(req, f, "audio/mpeg");
  if (ext && strcasecmp(ext, ".mp4") == 0) return send_media_file(req, f, "video/mp4");
  if (ext && strcasecmp(ext, ".m4a") == 0) return send_media_file(req, f, "audio/mp4");

  if (ext) {
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
      httpd_resp_set_type(req, "text/html; charset=utf-8");
    } else if (strcasecmp(ext, ".css") == 0) {
      httpd_resp_set_type(req, "text/css; charset=utf-8");
    } else if (strcasecmp(ext, ".js") == 0) {
      httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    } else if (strcasecmp(ext, ".webp") == 0) {
      httpd_resp_set_type(req, "image/webp");
    } else if (strcasecmp(ext, ".png") == 0) {
      httpd_resp_set_type(req, "image/png");
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
      httpd_resp_set_type(req, "image/jpeg");
    } else if (strcasecmp(ext, ".svg") == 0) {
      httpd_resp_set_type(req, "image/svg+xml");
    } else if (strcasecmp(ext, ".json") == 0) {
      httpd_resp_set_type(req, "application/json");
    } else {
      httpd_resp_set_type(req, "text/plain; charset=utf-8");
    }
  } else {
    httpd_resp_set_type(req, "application/octet-stream");
  }

  char chunk[1024];
  size_t read_bytes;
  while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
      fclose(f);
      httpd_resp_sendstr_chunk(req, NULL);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t send_chunk_string(httpd_req_t *req, const char *str) {
  size_t total_len = strlen(str);
  size_t sent = 0;
  const size_t chunk_size = 2048;

  while (sent < total_len) {
    size_t cur_len = total_len - sent;
    if (cur_len > chunk_size) {
      cur_len = chunk_size;
    }
    if (httpd_resp_send_chunk(req, str + sent, cur_len) != ESP_OK) {
      ESP_LOGE(TAG, "[CHUNK] Send failed at offset %d/%d", (int)sent,
               (int)total_len);
      httpd_resp_sendstr_chunk(req, NULL);
      return ESP_FAIL;
    }
    sent += cur_len;
  }
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t help_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, HTML_HELP_GUIDE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
  if (network_get_mode() == NET_MODE_AP_CAPTIVE) {
    return render_setup_page(req);
  }

  // 쿼리 스트링 확인 (?view=showcase, ?view=help, ?view=admin)
  char query[64] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char view_val[32] = {0};
    if (httpd_query_key_value(query, "view", view_val, sizeof(view_val)) ==
        ESP_OK) {
      if (strcmp(view_val, "showcase") == 0) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, HTML_MAIN, HTTPD_RESP_USE_STRLEN);
      } else if (strcmp(view_val, "help") == 0) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, HTML_HELP_GUIDE, HTTPD_RESP_USE_STRLEN);
      }
    }
  }

  // 관리자가 지정한 업로드 HTML을 IPv4/IPv6 루트 접속 화면으로 우선 제공
  char initial_page[128] = {0};
  if (get_initial_page(initial_page, sizeof(initial_page)) &&
      is_valid_initial_page(initial_page)) {
    char filepath[sizeof(STORAGE_BASE_PATH) + sizeof(initial_page)];
    snprintf(filepath, sizeof(filepath), "%s%s", STORAGE_BASE_PATH,
             initial_page);
    if (send_spiffs_file(req, filepath) == ESP_OK) return ESP_OK;
  }

  // 사용자 지정 페이지가 없으면 index.html을 기본 페이지로 제공
  if (send_spiffs_file(req, STORAGE_BASE_PATH "/index.html") == ESP_OK) {
    return ESP_OK;
  }

  // 2. 관리자가 지정한 첫 접속 화면 모드에 따라 분기
  // 0: 도움말/가이드 (기본값)
  // 1: 메인 시스템 쇼케이스 (HTML_MAIN)
  // 2: 관리자 대시보드 리다이렉트 (/admin)
  int view_mode = get_initial_view_mode();
  if (view_mode == 1) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, HTML_MAIN, HTTPD_RESP_USE_STRLEN);
  } else if (view_mode == 2) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/admin");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
  }

  // 기본값: 도움말 가이드 화면 서빙
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, HTML_HELP_GUIDE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t setup_get_handler(httpd_req_t *req) {
  return render_setup_page(req);
}

static esp_err_t captive_portal_redirect(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static void url_decode(char *dst, const char *src) {
  char a, b;
  while (*src) {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a' - 'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a' - 'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16 * a + b;
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst++ = '\0';
}

/* Decode an HTTP filename without accepting malformed escapes or embedded NUL. */
static bool decode_file_url(char *dst, size_t capacity, const char *src, bool query) {
  size_t out = 0;
  while (*src) {
    unsigned char c = (unsigned char)*src++;
    if (c == '%') {
      if (!src[0] || !src[1] || !isxdigit((unsigned char)src[0]) || !isxdigit((unsigned char)src[1])) return false;
      unsigned char a = (unsigned char)tolower((unsigned char)src[0]);
      unsigned char b = (unsigned char)tolower((unsigned char)src[1]);
      c = (unsigned char)(((a <= '9' ? a - '0' : a - 'a' + 10) << 4) |
                          (b <= '9' ? b - '0' : b - 'a' + 10));
      src += 2;
    } else if (query && c == '+') c = ' ';
    if (c < 32 || c == 127 || c == '\\' || out + 1 >= capacity) return false;
    dst[out++] = (char)c;
  }
  dst[out] = '\0';
  return true;
}

static void restart_task(void *arg) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  esp_restart();
}

static esp_err_t send_product_qr_inline(httpd_req_t *req, const char *url) {
  QRCode qr; uint8_t data[qrcode_getBufferSize(5)];
  if (qrcode_initText(&qr, data, 5, ECC_LOW, url) != 0) return ESP_FAIL;
  int width = qr.size + 8;
  char chunk[320];
  snprintf(chunk, sizeof(chunk), "<svg xmlns='http://www.w3.org/2000/svg' role='img' aria-label='제품 접속 QR' viewBox='0 0 %d %d' width='180' height='180'><rect width='100%%' height='100%%' fill='white'/>", width, width);
  esp_err_t err = httpd_resp_sendstr_chunk(req, chunk);
  for (uint8_t y = 0; err == ESP_OK && y < qr.size; ++y) {
    for (uint8_t x = 0; err == ESP_OK && x < qr.size; ++x) {
      if (!qrcode_getModule(&qr, x, y)) continue;
      snprintf(chunk, sizeof(chunk), "<rect x='%u' y='%u' width='1' height='1' fill='black'/>", (unsigned)x + 4, (unsigned)y + 4);
      err = httpd_resp_sendstr_chunk(req, chunk);
    }
  }
  return err == ESP_OK ? httpd_resp_sendstr_chunk(req, "</svg>") : err;
}

static bool decode_wifi_field(char *out, size_t capacity, const char *encoded) {
  size_t used = 0;
  while (*encoded) {
    unsigned char c = (unsigned char)*encoded++;
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (!encoded[0] || !encoded[1] ||
          !isxdigit((unsigned char)encoded[0]) ||
          !isxdigit((unsigned char)encoded[1])) return false;
      unsigned char a = (unsigned char)tolower((unsigned char)encoded[0]);
      unsigned char b = (unsigned char)tolower((unsigned char)encoded[1]);
      c = ((a <= '9' ? a - '0' : a - 'a' + 10) << 4) |
          (b <= '9' ? b - '0' : b - 'a' + 10);
      encoded += 2;
    }
    if (c < 32 || c == 127 || used + 1 >= capacity) return false;
    out[used++] = (char)c;
  }
  if (!capacity) return false;
  out[used] = '\0';
  return true;
}

#include "web_network_setup.inc"

static esp_err_t api_logs_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    httpd_resp_send(req, s_log_buffer, HTTPD_RESP_USE_STRLEN);
    xSemaphoreGive(s_log_mutex);
  } else {
    httpd_resp_send(req, "로그 버퍼 접근 대기 중...", HTTPD_RESP_USE_STRLEN);
  }
  return ESP_OK;
}

static esp_err_t api_system_get_handler(httpd_req_t *req) {
  uint32_t free_heap = esp_get_free_heap_size();
  uint32_t min_heap = esp_get_minimum_free_heap_size();
  uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
  uint32_t used_heap = (total_heap > free_heap) ? (total_heap - free_heap) : 0;
  int64_t uptime_sec = esp_timer_get_time() / 1000000;

  char ip_str[32] = "미연결";
  network_get_ip_str(ip_str, sizeof(ip_str));

  char ip6_str[64] = "미발급";
  network_get_ip6_str(ip6_str, sizeof(ip6_str));
  char ip6_linklocal_str[64] = "미발급";
  network_get_ip6_linklocal_str(ip6_linklocal_str, sizeof(ip6_linklocal_str));

  char saved_ssid[64] = "없음";
  char saved_pass[64] = {0};
  network_has_saved_credentials(saved_ssid, sizeof(saved_ssid), saved_pass,
                                sizeof(saved_pass));

  int8_t rssi = network_get_rssi();
  net_mode_t mode = network_get_mode();

  const esp_partition_t *running = esp_ota_get_running_partition();
  const char *running_label = running ? running->label : "factory";
  esp_app_desc_t app_desc;
  const char *app_ver = "1.0.0";
  if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
    app_ver = app_desc.version;
  }

  int init_view = get_initial_view_mode();

  uint32_t flash_total = 0;
  size_t disk_total = 0, disk_used = 0;
  bool storage_valid = esp_flash_get_size(NULL, &flash_total) == ESP_OK &&
                       esp_spiffs_info("littlefs", &disk_total, &disk_used) == ESP_OK &&
                       flash_total >= disk_total && disk_total >= disk_used;
  if (!storage_valid) {
    flash_total = 0;
    disk_total = disk_used = 0;
  }

  char product_url[96] = {0};
  network_get_mdns_url(product_url, sizeof(product_url));
  char cloud_url[96] = {0};
  cloud_relay_get_url(cloud_url, sizeof(cloud_url));
  char json[1792];
  snprintf(
      json, sizeof(json),
      "{"
      "\"free_heap\":%lu,"
      "\"min_heap\":%lu,"
      "\"total_heap\":%lu,"
      "\"used_heap\":%lu,"
      "\"uptime_sec\":%lld,"
      "\"ip\":\"%s\","
      "\"ip6\":\"%s\","
      "\"ip6_linklocal\":\"%s\","
      "\"ip6_status\":\"%s\","
      "\"mdns_url\":\"%s\","
      "\"mdns_ready\":%s,"
      "\"product\":\"%s\","
      "\"product_revision\":\"%s\","
      "\"serial\":\"%s\","
      "\"ssid\":\"%s\","
      "\"rssi\":%d,"
      "\"mode\":%d,"
      "\"ota_slot\":\"%s\","
      "\"app_ver\":\"%s\","
      "\"init_view\":%d,"
      "\"storage_valid\":%s,"
      "\"flash_total\":%lu,"
      "\"system_reserved\":%lu,"
      "\"disk_total\":%lu,"
      "\"disk_used\":%lu,"
      "\"tunnel_url\":\"%s\","
      "\"cloud_status\":\"%s\""
      "}",
      (unsigned long)free_heap, (unsigned long)min_heap,
      (unsigned long)total_heap, (unsigned long)used_heap,
      (long long)uptime_sec, ip_str, ip6_str, ip6_linklocal_str, network_get_ipv6_status(), product_url, network_mdns_is_ready() ? "true" : "false",
      network_get_product_name(), network_get_product_revision(), network_get_product_serial(), saved_ssid, (int)rssi, (int)mode,
      running_label, app_ver, init_view, storage_valid ? "true" : "false",
      (unsigned long)flash_total, (unsigned long)(flash_total - disk_total),
      (unsigned long)disk_total, (unsigned long)disk_used, cloud_url, cloud_relay_status());

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_qr_get_handler(httpd_req_t *req) {
  char url_buf[256] = {0}, query[1024] = {0}, encoded[768] = {0};
  bool download = false;
  if (httpd_req_get_url_query_len(req) >= sizeof(query))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "QR 주소가 너무 깁니다.");
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "url", encoded, sizeof(encoded)) == ESP_OK &&
        !decode_file_url(url_buf, sizeof(url_buf), encoded, true))
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "QR 주소 형식을 확인하세요.");
    char dl_val[16] = {0};
    download = httpd_query_key_value(query, "dl", dl_val, sizeof(dl_val)) == ESP_OK && strcmp(dl_val, "1") == 0;
  }
  if (!*url_buf && network_get_mdns_url(url_buf, sizeof(url_buf)) != ESP_OK) {
    char ip[32] = {0}; network_get_ip_str(ip, sizeof(ip));
    snprintf(url_buf, sizeof(url_buf), "http://%s/", ip);
  }
  QRCode qrcode;
  uint8_t *data = malloc(qrcode_getBufferSize(10));
  if (!data) return httpd_resp_send_500(req);
  int8_t result = qrcode_initText(&qrcode, data, 3, ECC_LOW, url_buf);
  if (result) result = qrcode_initText(&qrcode, data, 5, ECC_LOW, url_buf);
  if (result) result = qrcode_initText(&qrcode, data, 10, ECC_LOW, url_buf);
  if (result) { free(data); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "QR로 표현할 수 없는 주소입니다."); }
  int full_size = (qrcode.size + 8) * 8;
  httpd_resp_set_type(req, "image/svg+xml; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (download) httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"qr_code.svg\"");
  char header[320];
  snprintf(header, sizeof(header), "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\"><rect width=\"100%%\" height=\"100%%\" fill=\"white\"/><path fill=\"black\" d=\"", full_size, full_size, full_size, full_size);
  esp_err_t sent = httpd_resp_sendstr_chunk(req, header);
  char chunk[768]; size_t used = 0;
  for (uint8_t y = 0; y < qrcode.size && sent == ESP_OK; ++y) {
    for (uint8_t x = 0; x < qrcode.size && sent == ESP_OK; ++x) {
      if (!qrcode_getModule(&qrcode, x, y)) continue;
      char cell[64]; int n = snprintf(cell, sizeof(cell), "M%d %dh8v8h-8z ", (x + 4) * 8, (y + 4) * 8);
      if (used + (size_t)n >= sizeof(chunk)) { sent = httpd_resp_send_chunk(req, chunk, used); used = 0; }
      if (sent == ESP_OK) { memcpy(chunk + used, cell, n); used += n; }
    }
  }
  free(data);
  if (sent == ESP_OK && used) sent = httpd_resp_send_chunk(req, chunk, used);
  if (sent == ESP_OK) sent = httpd_resp_sendstr_chunk(req, "\"/></svg>");
  return sent == ESP_OK ? httpd_resp_sendstr_chunk(req, NULL) : sent;
}

static esp_err_t api_files_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  size_t total_bytes = 0, used_bytes = 0;
  if (esp_spiffs_info("littlefs", &total_bytes, &used_bytes) != ESP_OK) return httpd_resp_send_500(req);

  DIR *dir = opendir(STORAGE_BASE_PATH);
  if (!dir) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if (httpd_resp_sendstr_chunk(req, "{\"total\":") != ESP_OK) { closedir(dir); return ESP_FAIL; }
  char num_buf[96];
  snprintf(num_buf, sizeof(num_buf), "%u,\"used\":%u,\"max_name_bytes\":%u,\"files\":[",
           (unsigned int)total_bytes, (unsigned int)used_bytes,
           (unsigned int)(CONFIG_SPIFFS_OBJ_NAME_LEN - 2));
  if (httpd_resp_sendstr_chunk(req, num_buf) != ESP_OK) { closedir(dir); return ESP_FAIL; }

  struct dirent *ent;
  bool first = true;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.' || strstr(ent->d_name, ".aibak")) continue;
    if (ent->d_type == DT_REG || ent->d_type == DT_UNKNOWN) {
      char filepath[280];
      snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_BASE_PATH,
               ent->d_name);
      struct stat st;
      size_t fsize = 0;
      if (stat(filepath, &st) == 0) {
        fsize = st.st_size;
      }

      cJSON *entry = cJSON_CreateObject();
      if (!entry) { closedir(dir); return ESP_FAIL; }
      cJSON_AddStringToObject(entry, "name", ent->d_name);
      cJSON_AddNumberToObject(entry, "size", fsize);
      int64_t modified = file_date_read(ent->d_name);
      if (modified) cJSON_AddNumberToObject(entry, "modified_at", (double)modified);
      else cJSON_AddNullToObject(entry, "modified_at");
      char *item = cJSON_PrintUnformatted(entry); cJSON_Delete(entry);
      if (!item) { closedir(dir); return ESP_FAIL; }
      esp_err_t sent = first ? ESP_OK : httpd_resp_sendstr_chunk(req, ",");
      if (sent == ESP_OK) sent = httpd_resp_sendstr_chunk(req, item);
      free(item);
      if (sent != ESP_OK) { closedir(dir); return sent; }
      first = false;
    }
  }
  closedir(dir);

  esp_err_t result = httpd_resp_sendstr_chunk(req, "]}");
  if (result != ESP_OK) return result;
  return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t api_upload_post_handler(httpd_req_t *req) {
  char filename[128] = {0};
  if (httpd_req_get_hdr_value_str(req, "X-Filename", filename, sizeof(filename)) != ESP_OK) {
    char query[512] = {0}, encoded[384] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "filename", encoded, sizeof(encoded)) != ESP_OK)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or oversized filename");
    if (!decode_file_url(filename, sizeof(filename), encoded, true))
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename encoding or length");
  }
  if (strlen(filename) > CONFIG_SPIFFS_OBJ_NAME_LEN - 2)
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Filename exceeds storage limit");
  for (const unsigned char *c = (const unsigned char *)filename; *c; ++c)
    if (*c < 32 || *c == 127 || *c == '\\') return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
  if (strlen(filename) == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Missing filename");
    return ESP_FAIL;
  }

  // 파일명 보안 검증 (/ 또는 .. 차단)
  if (strchr(filename, '/') != NULL || strstr(filename, "..") != NULL ||
      filename[0] == '.' || strstr(filename, ".aibak") != NULL) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid filename");
    return ESP_FAIL;
  }

  char filepath[280];
  snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_BASE_PATH, filename);

  size_t total_bytes = 0, used_bytes = 0;
  if (esp_spiffs_info("littlefs", &total_bytes, &used_bytes) != ESP_OK)
    return httpd_resp_send_500(req);
  struct stat previous;
  size_t old_size = stat(filepath, &previous) == 0 ? (size_t)previous.st_size : 0;
  size_t free_bytes = total_bytes > used_bytes ? total_bytes - used_bytes : 0;
  if ((size_t)req->content_len > free_bytes + old_size) {
    httpd_resp_set_status(req, "507 Insufficient Storage");
    return httpd_resp_sendstr(req, "Not enough storage; reduce files or use cloud media");
  }

  FILE *f = fopen(filepath, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char buf[1024];
  int remaining = req->content_len;
  unsigned int receive_timeouts = 0;
  while (remaining > 0) {
    int to_read = MIN(remaining, sizeof(buf));
    int received = httpd_req_recv(req, buf, to_read);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT && ++receive_timeouts < 3) {
        continue;
      }
      ESP_LOGE(TAG, "Upload receive failed");
      fclose(f);
      unlink(filepath);
      file_date_remove(filename);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    receive_timeouts = 0;
    if (fwrite(buf, 1, received, f) != received) {
      ESP_LOGE(TAG, "File write failed (disk full?)");
      fclose(f);
      unlink(filepath);
      file_date_remove(filename);
      httpd_resp_set_status(req, "507 Insufficient Storage");
      httpd_resp_sendstr(req, "Disk full or write error");
      return ESP_FAIL;
    }
    remaining -= received;
  }
  if (fclose(f) != 0) {
    unlink(filepath); file_date_remove(filename);
    httpd_resp_set_status(req, "507 Insufficient Storage");
    return httpd_resp_sendstr(req, "File flush failed");
  }
  file_date_write(filename, file_date_request(req));

  ESP_LOGI(TAG, "File uploaded successfully: %s (%d bytes)", filepath,
           req->content_len);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}

static void ota_reboot_task(void *pvParameter) {
  ESP_LOGI(TAG, "Rebooting in 2 seconds after successful OTA update...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}

static esp_err_t api_ota_post_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Starting Firmware OTA Update (Size: %d bytes)",
           req->content_len);

  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    ESP_LOGE(TAG, "Failed to find valid OTA update partition");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "No valid OTA partition found");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG,
           "Writing firmware to partition '%s' at offset 0x%lx (size: 0x%lx)",
           update_partition->label, (unsigned long)update_partition->address,
           (unsigned long)update_partition->size);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err =
      esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "OTA begin failed");
    return ESP_FAIL;
  }

  char buf[1024];
  int remaining = req->content_len;
  while (remaining > 0) {
    int to_read = MIN(remaining, sizeof(buf));
    int received = httpd_req_recv(req, buf, to_read);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "OTA payload receive timeout/error");
      esp_ota_abort(ota_handle);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    err = esp_ota_write(ota_handle, (const void *)buf, received);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
      esp_ota_abort(ota_handle);
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "OTA flash write error");
      return ESP_FAIL;
    }
    remaining -= received;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "OTA validation/end error");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)",
             esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "Failed to set boot partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Firmware update successful! Boot partition set to '%s'.",
           update_partition->label);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"partition\":\"");
  httpd_resp_sendstr_chunk(req, update_partition->label);
  httpd_resp_sendstr_chunk(req, "\"}");
  httpd_resp_sendstr_chunk(req, NULL);

  xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
  return ESP_OK;
}

static esp_err_t api_delete_post_handler(httpd_req_t *req) {
  char filename[128] = {0};

  char query[512] = {0}, encoded[384] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "filename", encoded, sizeof(encoded)) == ESP_OK) {
    if (!decode_file_url(filename, sizeof(filename), encoded, true))
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename encoding");
  }

  if (strlen(filename) == 0 && req->content_len > 0 &&
      req->content_len < sizeof(filename)) {
    int ret = httpd_req_recv(req, filename, req->content_len);
    if (ret > 0) {
      filename[ret] = '\0';
    }
  }

  if (strlen(filename) == 0 || strchr(filename, '/') != NULL ||
      strstr(filename, "..") != NULL || strchr(filename, '\\') != NULL) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid filename");
    return ESP_FAIL;
  }

  char filepath[280];
  snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_BASE_PATH, filename);

  if (unlink(filepath) == 0) {
    file_date_remove(filename);
    ESP_LOGI(TAG, "File deleted: %s", filepath);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to delete: %s", filepath);
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "File not found or delete failed");
    return ESP_FAIL;
  }
}

static esp_err_t api_ipv6_retry_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "[API] POST /api/ipv6_retry requested");
    esp_err_t err = network_retry_ipv6();
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    if (err == ESP_OK) return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"공유기에 IPv6 주소 정보를 다시 요청했습니다.\"}");
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"STA 연결과 IPv6 자동 구성 설정을 확인하세요.\"}");
}

/* Download from SPIFFS directly, even when an SD file has the same name. */
static esp_err_t api_file_download_handler(httpd_req_t *req) {
  char query[384], encoded[384], name[128], full[280];
  if (httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
      httpd_query_key_value(query,"filename",encoded,sizeof(encoded))!=ESP_OK ||
      !decode_file_url(name,sizeof(name),encoded,true) || !name[0] || name[0]=='.' ||
      strchr(name,'/') || strchr(name,'\\') || strstr(name,".."))
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid filename");
  snprintf(full,sizeof(full),"%s/%s",STORAGE_BASE_PATH,name);
  FILE *file=fopen(full,"rb");
  if(!file) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"File not found");
  httpd_resp_set_type(req,"application/octet-stream");
  httpd_resp_set_hdr(req,"Content-Disposition","attachment");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
  char buffer[1024];size_t n;esp_err_t result=ESP_OK;
  while(result==ESP_OK && (n=fread(buffer,1,sizeof(buffer),file))>0) result=httpd_resp_send_chunk(req,buffer,n);
  if(ferror(file)) result=ESP_FAIL;
  fclose(file);
  return result==ESP_OK?httpd_resp_sendstr_chunk(req,NULL):result;
}

static esp_err_t spiffs_file_get_handler(httpd_req_t *req) {
  const char *uri = req->uri;
  // 쿼리 스트링 제거
  char clean_name[128] = {0};
  const char *qmark = strchr(uri, '?');
  size_t len = qmark ? (size_t)(qmark - uri) : strlen(uri);
  char encoded[384];
  if (len >= sizeof(encoded)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Path too long");
  memcpy(encoded, uri, len); encoded[len] = '\0';
  if (!decode_file_url(clean_name, sizeof(clean_name), encoded, false))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path encoding");

  if (strstr(clean_name, "..") != NULL) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_sendstr(req, "Forbidden");
    return ESP_FAIL;
  }

  char filepath[280];
  snprintf(filepath, sizeof(filepath), "%s%s", STORAGE_BASE_PATH, clean_name);

  if (send_spiffs_file(req, filepath) == ESP_OK) {
    return ESP_OK;
  }

  httpd_resp_set_status(req, "404 Not Found");
  httpd_resp_sendstr(req, "File not found");
  return ESP_FAIL;
}

static esp_err_t api_settings_get_handler(httpd_req_t *req) {
  int init_view = get_initial_view_mode();
  char init_page[128] = {0};
  get_initial_page(init_page, sizeof(init_page));

  ESP_LOGI(TAG, "[SETTINGS] GET /api/settings - current init_view=%d",
           init_view);

  char json[256];
  snprintf(json, sizeof(json),
           "{\"status\":\"ok\",\"init_view\":%d,\"init_page\":\"%s\"}",
           init_view, init_page);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_settings_post_handler(httpd_req_t *req) {
  char buf[768] = {0};
  if (req->content_len <= 0 || req->content_len >= sizeof(buf)) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
                       "{\"status\":\"error\",\"message\":\"Invalid request body\"}");
    return ESP_FAIL;
  }

  int received_total = 0;
  while (received_total < req->content_len) {
    int received = httpd_req_recv(req, buf + received_total,
                                  req->content_len - received_total);
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (received <= 0) {
      ESP_LOGW(TAG, "[SETTINGS] POST /api/settings body receive failed");
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    received_total += received;
  }
  buf[received_total] = '\0';

  if (received_total == 0) {
    ESP_LOGW(TAG, "[SETTINGS] POST /api/settings failed: Empty body received");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req,
                       "{\"status\":\"error\",\"message\":\"Empty request\"}");
    return ESP_FAIL;
  }


  char pw_input[64] = {0};
  char new_pw[64] = {0};
  char init_view_str[16] = {0};
  char init_page[128] = {0};
  bool init_page_requested = false;
  bool restore_defaults_requested = false;
  char cloud_token[65] = {0};
  bool cloud_token_requested = false;

  char *token = strtok(buf, "&");
  while (token != NULL) {
    char *eq = strchr(token, '=');
    if (eq) {
      *eq = '\0';
      char *val = eq + 1;
      char decoded[768] = {0};
      url_decode(decoded, val);
      if (strcmp(token, "cloud_token") == 0) {
        cloud_token_requested = true;
        if (strlen(decoded) != 64) {
          httpd_resp_set_type(req, "application/json");
          httpd_resp_set_status(req, "400 Bad Request");
          return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid device token\"}");
        }
        memcpy(cloud_token, decoded, 64);
      }
      if ((strcmp(token, "cur_pw") == 0 || strcmp(token, "new_pw") == 0) && strlen(decoded) >= sizeof(pw_input)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"비밀번호는 UTF-8 기준 64바이트 미만이어야 합니다.\"}");
      }
      if (strcmp(token, "cur_pw") == 0) {
        strncpy(pw_input, decoded, sizeof(pw_input) - 1);
      } else if (strcmp(token, "new_pw") == 0) {
        strncpy(new_pw, decoded, sizeof(new_pw) - 1);
      } else if (strcmp(token, "init_view") == 0) {
        strncpy(init_view_str, decoded, sizeof(init_view_str) - 1);
      } else if (strcmp(token, "init_page") == 0) {
        if (strlen(decoded) >= sizeof(init_page)) {
          httpd_resp_set_status(req, "400 Bad Request");
          httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"페이지 경로가 너무 깁니다.\"}");
          return ESP_OK;
        }
        strncpy(init_page, decoded, sizeof(init_page) - 1);
        init_page_requested = true;
      } else if (strcmp(token, "restore_defaults") == 0 &&
                 strcmp(decoded, "1") == 0) {
        restore_defaults_requested = true;
      }
    }
    token = strtok(NULL, "&");
  }

  /* A signed-in browser (session cookie) may omit the password, except for a
   * password change or reset, which re-encrypt the stored AI key. */
  bool sensitive = new_pw[0] || restore_defaults_requested;
  bool signed_in = !pw_input[0] && ai_session_for(req) != NULL;
  if (!pw_input[0] && (!signed_in || sensitive)) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, sensitive
        ? "{\"status\":\"error\",\"message\":\"현재 관리자 비밀번호를 입력하세요.\"}"
        : "{\"status\":\"error\",\"message\":\"로그인이 만료되었습니다. 다시 로그인하세요.\"}");
  }
  /* Password checks also migrate old plaintext records to salted hashes. */
  if (!signed_in && !verify_admin_password(pw_input)) {
    ESP_LOGW(TAG, "[SETTINGS] Auth FAIL: Password mismatch");
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, esp_timer_get_time() < s_ai_login_blocked_until
        ? "{\"status\":\"error\",\"message\":\"로그인 실패 5회로 잠시 제한되었습니다. 표시된 대기 시간이 지난 후 다시 입력하세요.\"}"
        : "{\"status\":\"error\",\"message\":\"비밀번호가 일치하지 않습니다.\"}");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "[SETTINGS] Auth SUCCESS: %s", signed_in ? "session" : "admin password matched");
  /* Remember this browser: a password login gets a session cookie. */
  char auth_cookie[128] = {0};
  if (!signed_in && ai_session_for(req) == NULL) {
    ai_session_t *created = ai_session_create(pw_input);
    if (created) { auth_cookie_header(auth_cookie, created->token); httpd_resp_set_hdr(req, "Set-Cookie", auth_cookie); }
  }

  if (cloud_token_requested) {
    esp_err_t err = cloud_relay_provision(cloud_token);
    memset(cloud_token, 0, sizeof(cloud_token));
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Cannot save device token\"}");
    }
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Device registered. Reboot the board.\"}");
  }

  if (restore_defaults_requested) {
    ai_security_record_t record = {0};
    esp_err_t reset_err = ai_record_read(&record);
    if (reset_err == ESP_OK) { record.key_len = 0; ai_wipe(record.ciphertext, sizeof(record.ciphertext)); reset_err = ai_record_write(&record); }
    ai_wipe(&record, sizeof(record));
    if (reset_err == ESP_OK) reset_err = set_admin_password(DEFAULT_ADMIN_PW, pw_input);
    if (reset_err == ESP_OK) reset_err = set_initial_view_mode(0);
    if (reset_err == ESP_OK) reset_err = set_initial_page("");
    if (reset_err == ESP_OK) reset_err = esp_spiffs_format("littlefs");
    if (reset_err == ESP_OK) reset_err = file_dates_clear();
    if (reset_err == ESP_OK) web_factory_seed(true);
    if (reset_err != ESP_OK) {
      ESP_LOGE(TAG, "Storage/default restore failed: %s", esp_err_to_name(reset_err));
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_set_type(req, "application/json; charset=utf-8");
      httpd_resp_sendstr(req,
                         "{\"status\":\"error\",\"message\":\"저장소 초기화 또는 기본값 복원에 실패했습니다.\"}");
      return ESP_OK;
    }
    ESP_LOGW(TAG, "Web storage formatted and admin defaults restored; Wi-Fi credentials preserved");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req,
                       "{\"status\":\"ok\",\"message\":\"웹파일 저장소와 관리자 설정을 초기화했습니다.\"}");
    return ESP_OK;
  }

  // 새 비밀번호 변경 요청이 있는 경우
  if (strlen(new_pw) > 0) {
    if (strlen(new_pw) < 8) {
      ESP_LOGW(TAG, "[SETTINGS] Password change failed: Too short (<8 chars)");
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"새 "
                              "비밀번호는 최소 8자리 이상이어야 합니다.\"}");
      return ESP_OK;
    }
    if (set_admin_password(new_pw, pw_input) != ESP_OK) {
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"비밀번호 또는 암호화된 API 키 변경에 실패했습니다.\"}");
    }
    ESP_LOGI(TAG, "[SETTINGS] Admin password updated successfully in NVS");
    /* The change signed every session out; keep this browser signed in. */
    bool cookie_set = auth_cookie[0] != 0;
    ai_session_t *created = ai_session_create(new_pw);
    if (created) {
      auth_cookie_header(auth_cookie, created->token);
      if (!cookie_set) httpd_resp_set_hdr(req, "Set-Cookie", auth_cookie);
    }
  }

  // 첫 접속 화면 모드 변경 요청이 있는 경우
  if (strlen(init_view_str) > 0) {
    int mode = atoi(init_view_str);
    if (mode >= 0 && mode <= 2) {
      set_initial_view_mode(mode);
      ESP_LOGI(TAG, "[SETTINGS] Initial view mode updated to: %d in NVS", mode);
    }
  }

  if (init_page_requested) {
    if (init_page[0] != '\0' && !is_valid_initial_page(init_page)) {
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_sendstr(req,
                         "{\"status\":\"error\",\"message\":\"업로드된 HTML 경로를 확인하세요. 예: /welcome.html\"}");
      return ESP_OK;
    }
    esp_err_t page_err = set_initial_page(init_page);
    if (page_err != ESP_OK) {
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_sendstr(req,
                         "{\"status\":\"error\",\"message\":\"페이지 설정 저장에 실패했습니다.\"}");
      return ESP_OK;
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(
      req,
      "{\"status\":\"ok\",\"message\":\"설정이 성공적으로 저장되었습니다.\"}");
  return ESP_OK;
}

static esp_err_t admin_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  ESP_LOGI(
      TAG,
      "[ADMIN] HTTP GET /admin requested. Streaming dashboard (Chunked)...");
  esp_err_t res = send_chunk_string(req, HTML_ADMIN_DASHBOARD);
  if (res == ESP_OK) {
    ESP_LOGI(TAG, "[ADMIN] Dashboard streamed successfully to client.");
  } else {
    ESP_LOGE(TAG, "[ADMIN] Failed to stream dashboard to client!");
  }
  return res;
}

static void ai_page_headers(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self' data: blob:; frame-src 'self' blob:; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
}
static esp_err_t ai_chat_get_handler(httpd_req_t *req) {
  ai_page_headers(req);
  return send_chunk_string(req, HTML_AI_LOGIN);
}
static esp_err_t ai_studio_get_handler(httpd_req_t *req) {
  if (!ai_require_auth(req)) return ESP_OK;
  ai_page_headers(req);
  return send_chunk_string(req, HTML_AI_STUDIO);
}

#include "web_public_stream.inc"

esp_err_t web_server_start(void) {
  if (s_server) return ESP_OK;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // Setup rendering, Wi-Fi scanning and formatted logging share this stack.
  config.stack_size = 8192;
  config.max_uri_handlers = 40;
  config.uri_match_fn = httpd_uri_match_wildcard;
  /* Reserve descriptors for outbound TLS, mDNS, DNS and SNTP. */
  config.max_open_sockets = 4;
  config.lru_purge_enable = true;

  if (httpd_start(&s_server, &config) == ESP_OK) {
    const httpd_uri_t ai_routes[] = {
      {.uri="/api/auth/status", .method=HTTP_GET, .handler=api_auth_status_handler},
      {.uri="/api/ai/login", .method=HTTP_POST, .handler=ai_login_post_handler},
      {.uri="/api/ai/logout", .method=HTTP_POST, .handler=ai_logout_post_handler},
      {.uri="/api/ai/studio", .method=HTTP_GET, .handler=ai_studio_get_handler},
      {.uri="/api/ai/key", .method=HTTP_GET, .handler=ai_key_get_handler},
      {.uri="/api/ai/key", .method=HTTP_POST, .handler=ai_key_post_handler},
      {.uri="/api/ai/chat", .method=HTTP_POST, .handler=ai_chat_post_handler}
    };
    for (size_t i = 0; i < sizeof(ai_routes)/sizeof(ai_routes[0]); ++i) {
      if (httpd_register_uri_handler(s_server, &ai_routes[i]) != ESP_OK) {
        httpd_stop(s_server); s_server = NULL; return ESP_FAIL;
      }
    }
    httpd_uri_t agent_uri = {.uri = "/api/agent", .method = HTTP_POST,
                             .handler = api_agent_post_handler, .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &agent_uri);
    httpd_uri_t root_uri = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_uri_t help_uri = {.uri = "/help",
                            .method = HTTP_GET,
                            .handler = help_get_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &help_uri);

    httpd_uri_t admin_uri = {.uri = "/admin",
                             .method = HTTP_GET,
                             .handler = admin_get_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &admin_uri);

    httpd_uri_t ai_uri = {.uri = "/ai",
                          .method = HTTP_GET,
                          .handler = ai_chat_get_handler,
                          .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &ai_uri);

    httpd_uri_t api_settings_get_uri = {.uri = "/api/settings",
                                        .method = HTTP_GET,
                                        .handler = api_settings_get_handler,
                                        .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_settings_get_uri);

    httpd_uri_t api_settings_post_uri = {.uri = "/api/settings",
                                         .method = HTTP_POST,
                                         .handler = api_settings_post_handler,
                                         .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_settings_post_uri);

    httpd_uri_t api_sys_uri = {.uri = "/api/system",
                               .method = HTTP_GET,
                               .handler = api_system_get_handler,
                               .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_sys_uri);

    httpd_uri_t api_logs_uri = {.uri = "/api/logs",
                                .method = HTTP_GET,
                                .handler = api_logs_get_handler,
                                .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_logs_uri);

    httpd_uri_t api_qr_uri = {.uri = "/api/qr",
                              .method = HTTP_GET,
                              .handler = api_qr_get_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_qr_uri);

    const httpd_uri_t file_download_uri={.uri="/api/files/download",.method=HTTP_GET,.handler=api_file_download_handler};
    httpd_register_uri_handler(s_server,&file_download_uri);
    httpd_uri_t api_files_uri = {.uri = "/api/files",
                                 .method = HTTP_GET,
                                 .handler = api_files_get_handler,
                                 .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_files_uri);

    httpd_uri_t api_upload_uri = {.uri = "/api/upload",
                                  .method = HTTP_POST,
                                  .handler = api_upload_post_handler,
                                  .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_upload_uri);

    httpd_uri_t api_ota_uri = {.uri = "/api/ota",
                               .method = HTTP_POST,
                               .handler = api_ota_post_handler,
                               .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_ota_uri);

    httpd_uri_t api_delete_uri = {.uri = "/api/delete",
                                  .method = HTTP_POST,
                                  .handler = api_delete_post_handler,
                                  .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_delete_uri);

    httpd_uri_t api_ipv6_retry_uri = {.uri = "/api/ipv6_retry",
                                      .method = HTTP_POST,
                                      .handler = api_ipv6_retry_post_handler,
                                      .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &api_ipv6_retry_uri);

    httpd_uri_t setup_uri = {.uri = "/setup",
                             .method = HTTP_GET,
                             .handler = setup_get_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &setup_uri);

    httpd_uri_t wifi_post_uri = {.uri = "/api/wifi",
                                 .method = HTTP_POST,
                                 .handler = wifi_post_handler,
                                 .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &wifi_post_uri);
    httpd_uri_t network_get_uri = {.uri="/api/network", .method=HTTP_GET, .handler=api_network_get_handler};
    httpd_uri_t wifi_scan_uri = {.uri="/api/wifi/scan", .method=HTTP_GET, .handler=api_wifi_scan_handler};
    httpd_register_uri_handler(s_server, &network_get_uri);
    httpd_register_uri_handler(s_server, &wifi_scan_uri);

    httpd_uri_t hotplug_uri = {.uri = "/hotspot-detect.html",
                               .method = HTTP_GET,
                               .handler = captive_portal_redirect,
                               .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &hotplug_uri);

    httpd_uri_t generate_204_uri = {.uri = "/generate_204",
                                    .method = HTTP_GET,
                                    .handler = captive_portal_redirect,
                                    .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &generate_204_uri);

    // SPIFFS 정적 파일 와일드카드 매핑 (루트 외 모든 경로의 정적 파일 서빙
    // fallback)
    httpd_uri_t spiffs_wildcard_uri = {.uri = "/*",
                                       .method = HTTP_GET,
                                       .handler = spiffs_file_get_handler,
                                       .user_ctx = NULL};
    httpd_register_uri_handler(s_server, &spiffs_wildcard_uri);

    esp_err_t mdns_err = network_mdns_start();
    if (mdns_err != ESP_OK) ESP_LOGW(TAG, "mDNS unavailable; IP access remains available: %s", esp_err_to_name(mdns_err));
    ESP_LOGI(TAG, "HTTP Server started successfully (Admin, WebP & File "
                  "Manager Enabled)");
    atomic_store(&s_server_running, true);
    return ESP_OK;
  }
  ESP_LOGE(TAG, "Failed to start HTTP Server");
  return ESP_FAIL;
}

void web_server_stop(void) {
  atomic_store(&s_server_running, false);
  ai_sessions_clear();
  network_mdns_stop();
  if (s_server) {
    httpd_stop(s_server);
    s_server = NULL;
  }
}
