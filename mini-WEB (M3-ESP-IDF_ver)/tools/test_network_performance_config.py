import re
import unittest
from pathlib import Path

from configure_sdio_clock import update_clock

ROOT = Path(__file__).resolve().parents[1]


class ProfileTests(unittest.TestCase):
    def test_only_clock_changes_and_rollback_is_exact(self):
        text = "CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000\nCONFIG_USER_CHOICE=27\n"
        fast = update_clock(text, 40)
        self.assertEqual(fast, text.replace("=20000", "=40000"))
        self.assertEqual(update_clock(fast, 20), text)
        with self.assertRaises(ValueError):
            update_clock(text + text, 40)

    def test_active_config_defaults_and_migration_agree(self):
        settings = {"LWIP_TCP_SND_BUF_DEFAULT": "65534", "LWIP_TCP_WND_DEFAULT": "65534",
                    "LWIP_TCP_RECVMBOX_SIZE": "64", "LWIP_TCPIP_RECVMBOX_SIZE": "64",
                    "LWIP_TCP_SACK_OUT": "y", "MBEDTLS_SSL_OUT_CONTENT_LEN": "8192"}
        for name in ["sdkconfig", "sdkconfig.defaults", "cmake/memory_policy.cmake"]:
            text = (ROOT / name).read_text(encoding="utf-8")
            for key, value in settings.items():
                self.assertIn(f"CONFIG_{key}={value}", text, name)
        c6 = (ROOT / "c6_sdkconfig.defaults").read_text(encoding="utf-8")
        self.assertIn("CONFIG_ESP_SDIO_HIGH_SPEED=y", c6)
        clocks = []
        for name in ["sdkconfig", "sdkconfig.defaults"]:
            match = re.search(r"CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=(\d+)", (ROOT / name).read_text(encoding="utf-8"))
            self.assertIsNotNone(match)
            clocks.append(int(match.group(1)))
        self.assertEqual(clocks[0], clocks[1])
        self.assertIn(clocks[0], [20000, 40000])


if __name__ == "__main__":
    unittest.main()
