// CMake configuration only: no firmware compiler, build or flash invocation.
const fs = require('fs'), path = require('path'), assert = require('assert');
const {spawnSync} = require('child_process');
const root = path.resolve(__dirname, '..');
const base = path.join(root, 'results', 'm3-hosted-compat');
const slash = s => s.replace(/\\/g, '/');
const req = 'WIFI_CONFIG_STA_SET_RESERVED_VAL(p_a_sta->reserved, p_c_sta->bitmask);\n' +
  'WIFI_HE_STA_SET_RESERVED_VAL(p_a_sta->he_reserved, p_c_sta->he_bitmask);\n' +
  'other_ap->reserved = 1;\n';
const rsp = 'p_a_sta->reserved = WIFI_CONFIG_STA_GET_RESERVED_VAL(p_c_sta->bitmask);\n' +
  'sta_info->reserved = 0;\n';
const write = (name, value) => {fs.mkdirSync(path.dirname(name), {recursive: true}); fs.writeFileSync(name, value);};
function run(name, header, version = '1.4.1', request = req, response = rsp, success = true) {
  const dir = path.join(base, name), hosted = path.join(dir, 'hosted');
  const sources = path.join(hosted, 'host/drivers/rpc/core');
  write(path.join(sources, 'rpc_req.c'), request);
  write(path.join(sources, 'rpc_rsp.c'), response);
  write(path.join(dir, 'other.c'), '/* unchanged */');
  write(path.join(hosted, 'idf_component.yml'), `version: ${version}\n`);
  write(path.join(dir, 'sdk/components/esp_wifi/include/esp_wifi_types_generic.h'), header);
  write(path.join(dir, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.16)
project(hosted_compat_fixture LANGUAGES NONE)
function(idf_component_get_property out component property)
  if(NOT component STREQUAL "espressif__esp_hosted")
    message(FATAL_ERROR "Wrong registry component name")
  endif()
  if(property STREQUAL "COMPONENT_DIR")
    set(${'$'}{out} "${slash(hosted)}" PARENT_SCOPE)
  else()
    set(${'$'}{out} hosted_fixture PARENT_SCOPE)
  endif()
endfunction()
add_custom_target(hosted_fixture)
set_property(TARGET hosted_fixture PROPERTY SOURCES "${slash(sources)}/rpc_req.c;${slash(sources)}/rpc_rsp.c;${slash(dir)}/other.c")
set(ENV{IDF_PATH} "${slash(dir)}/sdk")
include("${slash(root)}/cmake/hosted_idf55_compat.cmake")
m3_hosted_idf55_compat()
get_target_property(result hosted_fixture SOURCES)
file(WRITE "${'$'}{CMAKE_BINARY_DIR}/sources.txt" "${'$'}{result}")
`);
  const args = ['-S', dir, '-B', path.join(dir, 'configured')];
  const configure = () => spawnSync('cmake', args, {encoding: 'utf8'});
  const result = configure();
  assert.equal(result.status === 0, success, result.stdout + result.stderr);
  assert.equal(fs.readFileSync(path.join(sources, 'rpc_req.c'), 'utf8'), request);
  assert.equal(fs.readFileSync(path.join(sources, 'rpc_rsp.c'), 'utf8'), response);
  if (!success) return;
  const selected = fs.readFileSync(path.join(dir, 'configured/sources.txt'), 'utf8');
  assert(selected.includes(slash(path.join(dir, 'other.c'))));
  if (!header.includes('reserved1')) {assert(!selected.includes('m3_hosted_compat')); return;}
  const generated = path.join(dir, 'configured/m3_hosted_compat');
  const outputReq = fs.readFileSync(path.join(generated, 'rpc_req.c'), 'utf8');
  const outputRsp = fs.readFileSync(path.join(generated, 'rpc_rsp.c'), 'utf8');
  assert.equal(outputReq.replace(/\r\n/g, '\n'), request.replace(/\r\n/g, '\n')
    .replace('WIFI_CONFIG_STA_SET_RESERVED_VAL(p_a_sta->reserved, p_c_sta->bitmask);', '/* M3 IDF5.5: STA reserved wire bits remain zero. */')
    .replace('WIFI_HE_STA_SET_RESERVED_VAL(p_a_sta->he_reserved, p_c_sta->he_bitmask);', '/* M3 IDF5.5: HE reserved wire bits remain zero. */'));
  assert.equal(outputRsp.replace(/\r\n/g, '\n'), response.replace(/\r\n/g, '\n').replace('p_a_sta->reserved = WIFI_CONFIG_STA_GET_RESERVED_VAL(p_c_sta->bitmask);',
    '/* M3 IDF5.5: do not import reserved wire bits into SDK config. */'));
  assert(outputReq.includes('other_ap->reserved = 1;') || request !== req);
  assert(outputRsp.includes('sta_info->reserved = 0;') || response !== rsp);
  const before = fs.statSync(path.join(generated, 'rpc_req.c')).mtimeMs;
  const again = configure(); assert.equal(again.status, 0, again.stdout + again.stderr);
  assert.equal(fs.statSync(path.join(generated, 'rpc_req.c')).mtimeMs, before);
}
const modern = 'uint32_t reserved1:26;\nuint32_t reserved2:19;\n';
run('modern', modern);
run('legacy', 'uint32_t reserved:26;\nuint32_t he_reserved:22;\n');
run('wrong-version', modern, '1.5.0', req, rsp, false);
run('changed-source', modern, '1.4.1', 'unexpected source', rsp, false);
const actual = path.join(root, 'managed_components/espressif__esp_hosted/host/drivers/rpc/core');
if (fs.existsSync(actual)) run('downloaded-component', modern, '1.4.1',
  fs.readFileSync(path.join(actual, 'rpc_req.c'), 'utf8'), fs.readFileSync(path.join(actual, 'rpc_rsp.c'), 'utf8'));
assert(fs.readFileSync(path.join(root, 'CMakeLists.txt'), 'utf8').includes('m3_hosted_idf55_compat()'));
assert(fs.readFileSync(path.join(root, 'm3.ps1'), 'utf8').includes("Join-Path $PSScriptRoot 'cmake'"));
console.log('PASS: Hosted reserved-bit compatibility, legacy SDK, fail-closed guards, unchanged originals and idempotent CMake configuration.');
