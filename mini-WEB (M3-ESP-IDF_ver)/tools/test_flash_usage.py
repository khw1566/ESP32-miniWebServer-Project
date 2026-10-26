"""ELF fixtures for the post-link usage patcher. No compiler or target linker."""
from pathlib import Path
import struct
import tempfile
import unittest

import flash_usage as usage


def elf_fixture(symbol=b'm3_flash_usage_blob', duplicate=False):
    raw = bytearray(0x500)
    raw[:16] = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<HHIIIIIHHHHHH', raw, 16, 2, 243, 1, 0, 0, 0x400, 0, 52, 0, 0, 40, 4, 0)
    data = usage.blob(usage.part_ids(), 0, {})
    # One marker is the ordinary validation string, the other is the actual table.
    raw[0x100:0x110] = usage.MAGIC
    raw[0x140:0x140+len(data)] = data
    names = b'\0' + symbol + b'\0'
    raw[0x380:0x380+len(names)] = names
    struct.pack_into('<IIIBBH', raw, 0x310, 1, 0x40000040, len(data), 1, 0, 1)
    if duplicate:
        raw[0x200:0x200+len(data)] = data
        struct.pack_into('<IIIBBH', raw, 0x320, 1, 0x40000100, len(data), 1, 0, 1)
    sections = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                (0, 1, 2, 0x40000000, 0x100, 0x200, 0, 0, 4, 0),
                (0, 2, 0, 0, 0x300, 48 if duplicate else 32, 3, 1, 4, 16),
                (0, 3, 0, 0, 0x380, len(names), 0, 0, 1, 0)]
    for i, section in enumerate(sections):
        struct.pack_into('<10I', raw, 0x400+i*40, *section)
    return raw


class FlashUsageTests(unittest.TestCase):
    def test_two_identical_markers_resolve_to_the_table_symbol(self):
        raw = elf_fixture()
        self.assertEqual(raw.count(usage.MAGIC), 2)
        self.assertEqual(usage.table_offset(raw, 156), 0x140)

    def test_old_local_symbol_is_supported_for_existing_builds(self):
        self.assertEqual(usage.table_offset(elf_fixture(b'blob'), 156), 0x140)

    def test_patch_changes_only_the_object_and_is_idempotent(self):
        before = elf_fixture()
        ids = usage.part_ids()
        data = usage.blob(ids, 123456, {'APP_UI': 123456})
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'fixture.elf'
            target.write_bytes(before)
            usage.patch(target, data)
            after = target.read_bytes()
            self.assertEqual(after[:0x140], before[:0x140])
            self.assertEqual(after[0x140:0x140+len(data)], data)
            self.assertEqual(after[0x140+len(data):], before[0x140+len(data):])
            usage.patch(target, data)
            self.assertEqual(target.read_bytes(), after)

    def test_wrong_or_ambiguous_symbols_do_not_modify_the_file(self):
        for raw in (elf_fixture(b'other'), elf_fixture(duplicate=True), bytearray(b'not an ELF')):
            with self.subTest(raw_size=len(raw)), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / 'bad.elf'
                target.write_bytes(raw)
                with self.assertRaises(SystemExit):
                    usage.patch(target, usage.blob(usage.part_ids(), 1, {}))
                self.assertEqual(target.read_bytes(), raw)

    def test_section_bounds_and_object_size_are_checked(self):
        raw = elf_fixture()
        struct.pack_into('<I', raw, 0x310+8, 155)
        with self.assertRaises(ValueError):
            usage.table_offset(raw, 156)
        raw = elf_fixture()
        struct.pack_into('<I', raw, 0x400+40+20, 0x80)
        with self.assertRaisesRegex(ValueError, 'outside'):
            usage.table_offset(raw, 156)
        with self.assertRaises(ValueError):
            usage.table_offset(elf_fixture()[:0x420], 156)

    def test_writable_or_unallocated_sections_are_rejected(self):
        for flags in (0, 1, 3, 6):
            raw = elf_fixture()
            struct.pack_into('<I', raw, 0x400+40+8, flags)
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                usage.table_offset(raw, 156)


if __name__ == '__main__':
    unittest.main()
