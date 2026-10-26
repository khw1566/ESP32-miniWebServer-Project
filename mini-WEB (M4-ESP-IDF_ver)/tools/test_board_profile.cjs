// Validate wiring/partition integration without compiling firmware.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..');
const read=name=>fs.readFileSync(path.join(root,name),'utf8');
const config=read('sdkconfig.defaults');
assert(config.includes('CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y')); 
assert(config.includes('CONFIG_ESP32P4_REV_MIN_100=y'));
assert(config.includes('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y'));
for(const line of ['CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y','CONFIG_SPIRAM_MODE_HEX=y',
 'CONFIG_SPIRAM_USE_MALLOC=y','CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y'])assert(config.includes(line),line);
const pins=read('main/include/board_storage.h');
const assignments=[...pins.matchAll(/#define BOARD_SD_(CMD|CLK|D0)\s+GPIO_NUM_(\d+)/g)];
assert.deepEqual(Object.fromEntries(assignments.map(m=>[m[1],Number(m[2])])),{CMD:44,CLK:43,D0:39});
for(const m of assignments)assert(![35,36,37].includes(Number(m[2])));
const sd=read('main/sd_media.c');
assert(sd.includes('slot.width = 4;'));assert(sd.includes('esp_vfs_fat_sdmmc_mount('));
assert(!sd.includes('sdspi'));assert(sd.includes('.format_if_mount_failed = false'));
let end=0;const rows=new Map();
for(const line of read('partitions_16mb.csv').split(/\r?\n/)){
 if(!line||line.startsWith('#'))continue;
 const [name,type,sub,offset,size]=line.split(',');const start=Number(offset),length=Number(size);
 assert(start>=end,`${name} overlaps`);assert(length>0);assert(start+length<=16*1024*1024);
 if(type==='app')assert.equal(start%65536,0,`${name} alignment`);
 rows.set(name,{type,sub,start,length});end=start+length;
}
assert.equal(end,16*1024*1024);
assert.equal(rows.get('ota_0').length,0x760000);assert.equal(rows.get('ota_1').length,0x760000);
assert.equal(rows.get('littlefs').length,0x110000);assert.equal(rows.get('nvs').start,0x9000,'NVS stays in place');
assert(read('main/web_server.c').includes('#define STORAGE_BASE_PATH SD_MEDIA_ROOT'));
assert(!read('main/web_server.c').includes('esp_vfs_spiffs_register('),'M4 file backend stays SD-only');
// Every explicit SPIFFS label used by the backend must exist as SPIFFS.
for(const file of ['web_server.c','storage_view.c','web_agent_diagnostics.inc']){
 for(const match of read('main/'+file).matchAll(/esp_spiffs_(?:info|format)\("([^"]+)"/g))
  assert.equal(rows.get(match[1])?.sub,'spiffs',`${file} partition ${match[1]}`);
}
assert(read('main/web_server.c').includes('if (!sd_media_ready()) return ESP_ERR_INVALID_STATE;'));
const cmake=read('main/CMakeLists.txt');
for(const source of ['sd_media.c','storage_view.c','https_memory.c','cloud_relay.c'])assert(cmake.includes('"'+source+'"'));
assert(!cmake.includes('esp_camera')); assert(cmake.includes('esp_lcd esp_lcd_jd9165 esp_lvgl_port'));
const project=read('CMakeLists.txt');
assert(project.includes('DEFINED ENV{IDF_TARGET}'));
assert(project.includes('DEFINED CACHE{IDF_TARGET}'));
assert(project.indexOf('message(FATAL_ERROR')<project.indexOf('include($ENV{IDF_PATH}'));
assert(pins.includes('!defined(CONFIG_IDF_TARGET_ESP32P4)'));
assert(read('main/cloud_relay.c').includes('network_get_product_name(), network_get_product_serial()'));
const startup=read('main/main.c');const runtime=read('main/m4_runtime.c');assert(runtime.includes('sd_media_init()'));assert(runtime.includes('cloud_relay_start()'));assert(startup.includes('m4_board_init()'));
console.log('PASS: SD pin roles, PSRAM allocation, native SD mount, 16MB partition bounds/labels, OTA alignment and M4 display and asynchronous service startup.');

const network=read('main/network_manager.c');
assert(network.includes('esp_wifi_init(&cfg)'));
assert(cmake.includes('esp_wifi_remote esp_hosted'));
assert(!cmake.includes('esp_eth'));
assert(sd.includes('host.pwr_ctrl_handle = sd_power'));
assert(sd.includes('host.slot = SDMMC_HOST_SLOT_0'));
assert(!sd.includes('BOARD_SD_POWER_EN')); for(const pin of ['BOARD_SD_D1','BOARD_SD_D2','BOARD_SD_D3'])assert(sd.includes(pin));
const deps=read('main/idf_component.yml');
assert(deps.includes('"1.6.5"')&&deps.includes('"2.13.0"'));
for(const line of ['CONFIG_WIFI_RMT_SOFTAP_SUPPORT=y','CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y',
 'CONFIG_ESP_HOSTED_SDIO_PIN_CLK=18','CONFIG_ESP_HOSTED_SDIO_PIN_CMD=19',
 'CONFIG_ESP_HOSTED_SDIO_PIN_D0=14','CONFIG_ESP_HOSTED_SDIO_PRIV_PIN_D1_4BIT_BUS=15',
 'CONFIG_ESP_HOSTED_SDIO_PIN_D2=16','CONFIG_ESP_HOSTED_SDIO_PIN_D3=17',
 'CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54'])assert(config.includes(line),line);
assert(read('main/network_mdns.inc').includes('#define PRODUCT_ADDRESS_NAME "m4"'));
console.log('PASS: M4 remote C6 Wi-Fi, exact component versions, SDIO pin config, TF slot0 and SD power.');
