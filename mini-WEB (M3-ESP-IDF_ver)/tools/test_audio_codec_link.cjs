// Configure a compiler-free CMake fixture; never build or flash firmware.
const fs = require('fs');
const os = require('os');
const path = require('path');
const assert = require('assert');
const {spawnSync} = require('child_process');
const root = path.resolve(__dirname, '..');
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'm3 codec link '));
try {
  const hook = path.join(root, 'cmake/audio_codec_link_compat.cmake').replaceAll('\\', '/');
  fs.writeFileSync(path.join(fixture, 'CMakeLists.txt'), `
cmake_minimum_required(VERSION 3.16)
project(codec_link_check LANGUAGES NONE)
set(IDF_TARGET esp32p4)
set(codec_dir "C:/Mini WebServer/mini-WEB (M3-ESP-IDF_ver)/managed_components/espressif__esp_audio_codec")
set(raw "-L \${codec_dir}/lib/\${IDF_TARGET}")
function(idf_component_get_property output component property)
  if(NOT component STREQUAL "espressif__esp_audio_codec")
    message(FATAL_ERROR "Unexpected component")
  endif()
  if(property STREQUAL "COMPONENT_LIB")
    set(\${output} codec PARENT_SCOPE)
  else()
    set(\${output} "\${codec_dir}" PARENT_SCOPE)
  endif()
endfunction()
add_library(codec INTERFACE)
set_property(TARGET codec PROPERTY LINK_LIBRARIES "\${raw};esp_sdec;esp_acodec;other_dependency")
set_property(TARGET codec PROPERTY INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:\${raw}>;$<LINK_ONLY:esp_sdec>;$<LINK_ONLY:esp_acodec>;-Lother_directory")
include("${hook}")
m3_audio_codec_link_compat()
m3_audio_codec_link_compat()
get_target_property(direct codec LINK_LIBRARIES)
get_target_property(transitive codec INTERFACE_LINK_LIBRARIES)
if(NOT direct STREQUAL "esp_sdec;esp_acodec;other_dependency")
  message(FATAL_ERROR "Incorrect direct links: \${direct}")
endif()
if(NOT transitive STREQUAL "$<LINK_ONLY:esp_sdec>;$<LINK_ONLY:esp_acodec>;-Lother_directory")
  message(FATAL_ERROR "Incorrect interface links: \${transitive}")
endif()
`);
  const result = spawnSync('cmake', ['-S', fixture, '-B', path.join(fixture, 'out')], {encoding:'utf8'});
  if (result.error) throw result.error;
  assert.equal(result.status, 0, result.stdout + result.stderr);
  const project = fs.readFileSync(path.join(root, 'CMakeLists.txt'), 'utf8');
  assert(project.indexOf('m3_audio_codec_link_compat()') > project.indexOf('project(mini-webserver-m3)'));
  console.log('PASS: compiler-free CMake configuration removes split codec paths, preserves archive targets and unrelated links, and is idempotent.');
} finally {
  fs.rmSync(fixture, {recursive:true, force:true});
}
