"""Exercise the real compiler-free staging and SD JSON installer."""
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest

import prepare_build

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT.parent/'results/m4-build-tests'
FIXTURES.mkdir(parents=True, exist_ok=True)


class BuildStageTests(unittest.TestCase):
    def test_memory_migration_preserves_v1_cpu_and_user_settings(self):
        with tempfile.TemporaryDirectory(prefix='m4-policy-test-', dir=FIXTURES) as tmp:
            folder=Path(tmp); config=folder/'sdkconfig'; script=folder/'policy.cmake'
            config.write_text('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y\nCONFIG_UNRELATED=27\n'
                              'CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y\n',encoding='utf-8')
            script.write_text('set(SDKCONFIG "'+config.as_posix()+'")\n'
                              'include("'+(ROOT/'cmake/memory_policy.cmake').as_posix()+'")\n'
                              'm4_memory_policy()\n',encoding='utf-8')
            result=subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            migrated=config.read_text(encoding='utf-8')
            self.assertIn('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y',migrated)
            self.assertNotIn('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y',migrated)
            self.assertIn('CONFIG_UNRELATED=27',migrated)
            self.assertIn('CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y',migrated)
            self.assertIn('# CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC is not set',migrated)
            self.assertIn('CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072',migrated)

    def test_public_snapshot_is_complete_and_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix='m4-stage-test-', dir=FIXTURES) as tmp:
            stage = Path(tmp) / 'stage'
            prepare_build.prepare(stage)
            for name in ('main/m4_board.c', 'main/m4_panel_init.c', 'main/m4_layout.c',
                         'main/idf_component.yml', 'components/m4_lua/CMakeLists.txt',
                         'sdkconfig.panel-v1.defaults', 'sdkconfig.rev3.defaults',
                         'tools/generate_web_gzip.py'):
                self.assertEqual((stage/name).read_bytes(), (ROOT/name).read_bytes())
            self.assertFalse((stage/'build').exists())
            self.assertFalse((stage/'managed_components').exists())
            self.assertFalse(any(stage.rglob('*_private.h')))
            with self.assertRaises(ValueError):
                prepare_build.prepare(stage)

    def test_cannot_stage_into_source_tree(self):
        for path in (ROOT, ROOT/'stage', ROOT.parent):
            with self.assertRaises(ValueError):
                prepare_build.prepare(path)

    def test_relay_token_json_and_wifi_preservation(self):
        script = ROOT.parent/'tools/copy_m4_sd_settings.py'
        spec = importlib.util.spec_from_file_location('m4_sd_installer', script)
        installer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(installer)
        with tempfile.TemporaryDirectory(prefix='m4-sd-test-', dir=FIXTURES) as tmp:
            folder=Path(tmp); sd=folder/'sd'; sd.mkdir()
            private=sd/'.private'; private.mkdir()
            wifi=private/'wifi.json'; wifi.write_text('{"version":1}',encoding='utf-8')
            source=folder/'fixture.json'
            source.write_text(json.dumps({'version':1,'tube_token':'fixture-only-token-0000000000'}),encoding='utf-8')
            installed=installer.install(source,sd)
            self.assertEqual(json.loads(installed.read_text())['tube_token'],'fixture-only-token-0000000000')
            self.assertEqual(wifi.read_text(),'{"version":1}')
            with self.assertRaises(ValueError):
                installer.install(source,sd)


if __name__ == '__main__':
    unittest.main()
