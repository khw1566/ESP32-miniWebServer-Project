// CMake configuration and source checks only: no firmware compiler, build or flash.
const fs = require('fs'), path = require('path'), assert = require('assert');
const {spawnSync} = require('child_process');
const root = path.resolve(__dirname, '..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n/g, '\n');
const slash = s => s.replace(/\\/g, '/');
const write = (name, value) => {fs.mkdirSync(path.dirname(name), {recursive: true}); fs.writeFileSync(name, value);};

// Hosted 1.4.1 idled RESET54 high whatever the choice said; 2.x honors the
// choice, so a saved ACTIVE_LOW must migrate to ACTIVE_HIGH (same pin level).
const saved = 'CONFIG_UNRELATED_USER_CHOICE=17\n# CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH is not set\n' +
  'CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y\nCONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54\nCONFIG_ESP_HOSTED_RESET_GPIO_ACTIVE_LOW=y\n';
function fixture(name, existing) {
  const dir = path.join(root, 'results', 'm4-hosted-v2', name);
  fs.rmSync(dir, {recursive: true, force: true});
  if (existing) write(path.join(dir, 'sdkconfig'), saved);
  write(path.join(dir, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.16)
include("${slash(root)}/cmake/hosted_v2_migration.cmake")
m4_hosted_v2_migration()
project(hosted_v2_fixture LANGUAGES NONE)
`);
  const run = () => spawnSync('cmake', ['-S', dir, '-B', path.join(dir, 'configured')], {encoding: 'utf8'});
  let result = run(); assert.equal(result.status, 0, result.stdout + result.stderr);
  if (!existing) { assert(!fs.existsSync(path.join(dir, 'sdkconfig'))); return; }
  const config = fs.readFileSync(path.join(dir, 'sdkconfig'), 'utf8').replace(/\r\n/g, '\n');
  for (const line of ['CONFIG_UNRELATED_USER_CHOICE=17\n', 'CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y\n',
    '# CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW is not set\n', 'CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54\n',
    '# CONFIG_ESP_HOSTED_RESET_GPIO_ACTIVE_LOW is not set\n']) assert(config.includes(line), line);
  assert(!/\nCONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y/.test(config));
  result = run(); assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.equal(fs.readFileSync(path.join(dir, 'sdkconfig'), 'utf8').replace(/\r\n/g, '\n'), config, 'migration is idempotent');
}
fixture('saved-config', true);
fixture('new-config', false);

const cmake = read('CMakeLists.txt');
assert(cmake.indexOf('m4_hosted_v2_migration()') > 0 && cmake.indexOf('m4_hosted_v2_migration()') < cmake.indexOf('project(mini-webserver-m4)'));
assert(!cmake.includes('hosted_idf55_compat') && !cmake.includes('hosted_dma_guard'), '1.4.1-only source patches are retired');
const deps = read('main/idf_component.yml');
assert(/esp_wifi_remote:\n\s+version: "1\.6\.5"/.test(deps) && /esp_hosted:\n\s+version: "2\.13\.0"/.test(deps));
const defaults = read('sdkconfig.defaults');
assert(defaults.includes('\nCONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y\n') && !defaults.includes('\nCONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y'));
assert(defaults.includes('CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54'));

// C6 image: same partition table as the module's flash, so it fits an OTA slot.
const c6 = read('c6_sdkconfig.defaults');
assert(c6.includes('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="m4_c6_partitions.csv"'));
assert(!c6.includes('# CONFIG_BT_ENABLED is not set'), 'BLE controller stays available on the C6');
const table = read('m4_c6_partitions.csv').split('\n').filter(l => l && !l.startsWith('#')).map(l => l.split(','));
const slots = table.filter(r => r[1] === 'app');
assert.deepEqual(slots.map(r => [r[0], r[3], r[4]]), [['ota_0', '0x10000', '1536K'], ['ota_1', '0x190000', '1536K']]);
const update = read('main/m4_c6_update.c');
assert(update.includes('#define C6_SLOT_BYTES  (1536U * 1024U)'));
assert(update.includes('#define C6_CHUNK_BYTES 1400U'), '1.4.1 slaves accept 1400-byte OTA writes');
assert(update.includes('image.chip_id != ESP_CHIP_ID_ESP32C6') && update.includes('ESP_APP_DESC_MAGIC_WORD'));
const order = ['esp_hosted_connect_to_slave()', 'esp_hosted_slave_ota_begin()', 'esp_hosted_slave_ota_write(chunk, n)',
  'esp_hosted_slave_ota_end()', 'retire(C6_DONE_PATH);\n    /* Slaves', 'esp_hosted_slave_ota_activate()', 'esp_restart()'];
order.reduce((at, token) => { const i = update.indexOf(token, at); assert(i > at, token); return i; }, -1);
// Every flash attempt retires the staged file so a bad image cannot boot-loop.
const failure = update.slice(update.indexOf('if (err != ESP_OK) {\n        ESP_LOGE(TAG, "C6 update failed'));
assert(failure.indexOf('retire(C6_FAILED_PATH)') < failure.indexOf('return err;'));
assert(update.includes('running.major1 == 2 && running.minor1 > 5'), 'activate only on slaves that implement it');
const network = read('main/network_manager.c');
assert(network.indexOf('m4_c6_update_from_sd()') > 0 && network.indexOf('m4_c6_update_from_sd()') < network.indexOf('esp_wifi_init(&cfg)'));
const runtime = read('main/m4_runtime.c');
assert(runtime.indexOf('sd_media_init()') < runtime.indexOf('network_init()'), 'SD is mounted before the C6 update check');
assert(read('main/CMakeLists.txt').match(/"m4_c6_update\.c"/g).length === 1);
const prepare = read('prepare_c6.ps1');
assert(prepare.includes('2\\.13\\.0') && prepare.includes("'m4_c6_partitions.csv'") && prepare.includes('c6_update.bin'));
console.log('PASS: Hosted 2.13 reset-pin migration (idempotent), retired 1.4.1 patches, C6 partition/slot bounds, SD update order, failure retirement and activate gating.');
