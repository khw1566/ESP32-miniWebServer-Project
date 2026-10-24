"""Host checks: deployment behavior with fake secrets and firmware integration."""
import json
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest
from copy_m3_sd_settings import install, install_wifi, install_relays

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'mini-WEB (M3-ESP-IDF_ver)/main'


class SettingsTests(unittest.TestCase):
    def test_relays_only_cli_preserves_api_keys_and_wifi(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp); sd = root / 'sd'; private = sd / '.private'
            private.mkdir(parents=True)
            api = private / 'api_keys.json'; api.write_bytes(b'preserve-api-fixture')
            wifi = private / 'wifi.json'; wifi.write_bytes(b'preserve-wifi-fixture')
            source = root / 'relays.json'
            source.write_text(json.dumps(dict(version=1, servers=[dict(url='https://relay.example', token='fake-fixture-token-for-tests-only')])))
            result = subprocess.run([sys.executable, str(ROOT / 'tools/copy_m3_sd_settings.py'), str(sd),
                                     '--relays-only', '--relay-source', str(source), '--source', str(api)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(api.read_bytes(), b'preserve-api-fixture')
            self.assertEqual(wifi.read_bytes(), b'preserve-wifi-fixture')
            self.assertTrue((private / 'relays.json').exists())

    def test_relay_copy_merges_servers_normalizes_utf16_and_preserves_wifi(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp); sd = root / 'sd'; sd.mkdir()
            source = root / 'relays.json'
            a = dict(url='https://relay-a.example', token='fake-token-a-' * 3)
            b = dict(url='https://relay-b.example', token='fake-token-b-' * 3)
            source.write_text(json.dumps(dict(version=1, servers=[a])), encoding='utf-16')
            target = install_relays(source, sd)
            wifi = target.with_name('wifi.json'); wifi.write_bytes(b'preserved-wifi')
            source.write_text(json.dumps(dict(version=1, servers=[b])))
            install_relays(source, sd)
            self.assertEqual(json.loads(target.read_text())['servers'], [b, a])
            self.assertEqual(wifi.read_bytes(), b'preserved-wifi')
            before = target.read_bytes()
            for url in ('http://relay.example', 'https://user:pass@relay.example', 'https://relay.example/path'):
                source.write_text(json.dumps(dict(version=1, servers=[dict(a, url=url)])))
                with self.assertRaises(ValueError):
                    install_relays(source, sd)
                self.assertEqual(target.read_bytes(), before)
            target.write_bytes(b'broken')
            source.write_text(json.dumps(dict(version=1, servers=[a])))
            with self.assertRaises(ValueError):
                install_relays(source, sd)
            self.assertEqual(target.read_bytes(), b'broken')

    def test_copy_preserves_wifi_and_refuses_accidental_key_replacement(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp)
            source = root / 'source.json'
            source.write_text(json.dumps(dict(version=1, gemini='fake-gemini-' * 3,
                                             google_tts='fake-google-' * 3, openai='fake-openai-' * 3)))
            sd = root / 'sd'; sd.mkdir()
            private = sd / '.private'; private.mkdir()
            wifi = private / 'wifi.json'; wifi.write_bytes(b'preserved wifi fixture')
            target = install(source, sd)
            self.assertEqual(json.loads(source.read_bytes()), json.loads(target.read_bytes()))
            with self.assertRaises(ValueError):
                install(source, sd)
            install(source, sd, replace=True)
            self.assertEqual(wifi.read_bytes(), b'preserved wifi fixture')

    def test_invalid_input_does_not_touch_card(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp); sd = root / 'sd'; sd.mkdir()
            source = root / 'source.json'
            for bad in ('[]', '{', '{"version":1}', json.dumps(dict(version=1, gemini='x' * 256))):
                source.write_text(bad)
                with self.assertRaises(ValueError):
                    install(source, sd)
                self.assertEqual(list(sd.iterdir()), [])

    def test_optional_azure_fields_and_region_validation(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp); sd = root / 'sd'; sd.mkdir()
            source = root / 'source.json'
            data = dict(version=1, gemini='fake-gemini-' * 3,
                        google_tts='fake-google-' * 3, openai='fake-openai-' * 3,
                        azure_speech='fake-azure-' * 3, azure_region='koreacentral')
            source.write_text(json.dumps(data))
            target = install(source, sd)
            before = target.read_bytes()
            for field, value in [('azure_speech', 'short'), ('azure_speech', 'x' * 256),
                                 ('azure_region', '../evil'), ('azure_region', 'koreacentral.evil')]:
                broken = dict(data); broken[field] = value
                source.write_text(json.dumps(broken))
                with self.assertRaises(ValueError):
                    install(source, sd, replace=True)
                self.assertEqual(target.read_bytes(), before)

    def test_firmware_uses_sd_keys_in_all_provider_paths(self):
        for name in ('m3_agent.c', 'm3_google_audio.c'):
            code = (MAIN / name).read_text(encoding='utf-8')
            for macro in ('M3_GEMINI_API_KEY', 'M3_GOOGLE_TTS_API_KEY', 'M3_OPENAI_API_KEY'):
                self.assertNotIn(macro, code)
        runtime = (MAIN / 'm3_runtime.c').read_text(encoding='utf-8')
        self.assertLess(runtime.index('m3_sd_settings_init();'), runtime.index('esp_err_t net = network_init();'))
        self.assertIn('m3_voice_set_key(m3_sd_openai_key())', runtime)
        self.assertIn('m3_sd_settings.c', (MAIN / 'CMakeLists.txt').read_text())

    def test_wifi_save_hook_is_opt_in_and_shared_copies_stay_synchronized(self):
        common = (ROOT / 'tools/firmware_common/network_settings.inc').read_text(encoding='utf-8')
        for p in ROOT.glob('mini-WEB*/main/network_settings.inc'):
            self.assertEqual(common, p.read_text(encoding='utf-8'))
        text = common
        self.assertLess(text.index('nvs_commit(h)'), text.index('NETWORK_SETTINGS_SAVED_HOOK(&clean)'))
        self.assertIn('#define NETWORK_SETTINGS_SAVE_HOOK m3_sd_wifi_store',
                      (MAIN / 'network_manager.c').read_text(encoding='utf-8'))

    def test_single_provider_without_version_is_supported(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp); sd = root / 'sd'; sd.mkdir()
            source = root / 'source.json'
            source.write_text(json.dumps(dict(gemini='fake-gemini-' * 3)))
            target = install(source, sd)
            self.assertEqual(json.loads(target.read_text()), dict(gemini='fake-gemini-' * 3))

    def test_utf16_wifi_and_keys_are_normalized_without_cross_overwrite(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp); sd = root / 'sd'; sd.mkdir()
            source = root / 'keys.json'; wifi = root / 'wifi.json'
            source.write_text(json.dumps(dict(gemini='fake-gemini-' * 3)), encoding='utf-16')
            target = install(source, sd)
            self.assertFalse(target.read_bytes().startswith(b'\xff\xfe'))
            before = target.read_bytes()
            data = dict(ssid='한글-Fixture', password='fixture-secret')
            wifi.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-16')
            installed = install_wifi(wifi, sd)
            self.assertEqual(json.loads(installed.read_text(encoding='utf-8')), data)
            self.assertEqual(target.read_bytes(), before)
            with self.assertRaises(ValueError):
                install_wifi(wifi, sd)
            data['password'] = 'short'
            wifi.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                install_wifi(wifi, sd, replace=True)
            self.assertEqual(json.loads(installed.read_text(encoding='utf-8'))['password'], 'fixture-secret')

    def test_private_paths_are_hidden_and_ignored(self):
        view = (MAIN / 'storage_view.c').read_text(encoding='utf-8')
        self.assertIn('!mutation_path(folder)', view)
        self.assertIn("entry->d_name[0] == '.'", view)
        self.assertIn('**/.private/', (ROOT / '.gitignore').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
