"""Exercise metadata failures using temporary CMake/compile-database fixtures."""
import json
from pathlib import Path
import tempfile
import unittest
from preflight import preflight


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name) / 'Firmware with spaces (P4)'
        (self.project / 'main').mkdir(parents=True)
        (self.project / 'build').mkdir()
        (self.project / 'CMakeLists.txt').write_text('project(fixture)', encoding='utf-8')
        (self.project / 'main/CMakeLists.txt').write_text('idf_component_register(SRCS "main.c")', encoding='utf-8')
        (self.project / 'main/main.c').write_text('void app_main(void) {}', encoding='utf-8')
        (self.project / 'sdkconfig').write_text('CONFIG_IDF_TARGET="esp32p4"\n', encoding='utf-8')
        self.cache = self.project / 'build/CMakeCache.txt'
        self.cache.write_text('CMAKE_HOME_DIRECTORY:INTERNAL=' + str(self.project) + '\nIDF_TARGET:STRING=esp32p4\n', encoding='utf-8')

    def report(self, sources=()):
        return preflight(self.project, sources=sources)

    def codes(self, report):
        return {f['code'] for f in report['findings']}

    def test_missing_metadata_is_not_build_success(self):
        r = self.report(['main/main.c'])
        self.assertEqual(r['target'], 'esp32p4')
        self.assertTrue(r['cmake_project_matches'])
        self.assertIn('compile_database_missing', self.codes(r))
        self.assertFalse(r['compiler_executed'])

    def test_stale_project_and_target(self):
        self.cache.write_text('CMAKE_HOME_DIRECTORY:INTERNAL=' + str(self.project.parent) + '\nIDF_TARGET:STRING=esp32\n', encoding='utf-8')
        self.assertTrue({'stale_cmake_project_directory', 'cmake_target_mismatch'} <= self.codes(self.report()))

    def test_invalid_database(self):
        p = self.project / 'build/compile_commands.json'
        for data in ['{broken', '{}', '[3]']:
            p.write_text(data, encoding='utf-8')
            self.assertIn('invalid_compile_database', self.codes(self.report()))

    def test_quoted_compiler_and_relative_source(self):
        compiler = self.project / 'tool with spaces.exe'
        compiler.write_bytes(b'fixture, never executable')
        data = [{'file': 'main/main.c', 'directory': str(self.project), 'command': '"' + str(compiler) + '" -c main/main.c'}]
        (self.project / 'build/compile_commands.json').write_text(json.dumps(data), encoding='utf-8')
        r = self.report(['main/main.c'])
        self.assertTrue(r['selected_sources'][0]['in_compile_database'])
        self.assertNotIn('recorded_compiler_not_found', self.codes(r))
        self.assertFalse(r['compiler_executed'])

    def test_outside_and_missing_sources(self):
        r = self.report(['../other.c', 'main/missing.c'])
        self.assertTrue({'source_outside_project', 'source_missing', 'check_component_source_registration'} <= self.codes(r))

    def test_installed_version_headers(self):
        sdk = Path(self.temp.name) / 'idf'
        header = sdk / 'components/esp_common/include/esp_idf_version.h'
        header.parent.mkdir(parents=True)
        header.write_text('\n'.join('#define ESP_IDF_VERSION_' + k + ' ' + v for k, v in zip(('MAJOR', 'MINOR', 'PATCH'), ('5', '5', '5'))), encoding='utf-8')
        self.assertEqual(preflight(self.project, sdk)['idf_version'], '5.5.5')


if __name__ == '__main__':
    unittest.main()
