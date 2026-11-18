"""Regression cases for extraction gaps that hid stock 415 PEI modules."""
import sys
from pathlib import Path
import struct
import unittest
import uuid
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import fwx


def section(kind, body):
    return (len(body) + 4).to_bytes(3, 'little') + bytes([kind]) + body


def amd(payload, declared=None):
    header = bytearray(256)
    struct.pack_into('<I', header, 0x14, len(payload) if declared is None else declared)
    return section(2, uuid.UUID(fwx.AMD_ZLIB_GUID).bytes_le + struct.pack('<HH', 24, 1) + header + payload)


class ExtractionRegression(unittest.TestCase):
    def test_padding_guid_does_not_hide_following_file(self):
        pad = bytearray(b'\xff' * 24)
        pad[18] = 0xf0
        pad[20:23] = (24).to_bytes(3, 'little')
        body = section(0x10, b'MZ-visible-after-pad')
        header = bytearray(24)
        header[:16] = uuid.UUID('01234567-89ab-cdef-0123-456789abcdef').bytes_le
        header[18] = 6
        header[20:23] = (24 + len(body)).to_bytes(3, 'little')
        data = bytes(pad + header + body)
        im = fwx.Img()
        im.files(data, 0, len(data), 0, 'test')
        self.assertEqual([b for _, b in im.blobs], [b'MZ-visible-after-pad'])

    def test_amd_recovers_executable(self):
        im = fwx.Img()
        im.sections(amd(zlib.compress(section(0x12, b'VZ-recovered'))), 'test', {})
        self.assertEqual([b for _, b in im.blobs], [b'VZ-recovered'])

    def test_amd_rejects_incomplete_stream(self):
        payload = zlib.compress(section(0x12, b'VZ-recovered'))[:-1]
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            fwx.Img().sections(amd(payload), 'test', {})

    def test_amd_rejects_trailing_stream_data(self):
        with self.assertRaisesRegex(ValueError, 'trailing'):
            fwx.Img().sections(amd(zlib.compress(b'example') + b'extra'), 'test', {})

    def test_amd_rejects_wrong_compressed_size(self):
        with self.assertRaisesRegex(ValueError, 'size mismatch'):
            fwx.Img().sections(amd(zlib.compress(b'example'), 1), 'test', {})

    def test_amd_rejects_short_header(self):
        buf = section(2, uuid.UUID(fwx.AMD_ZLIB_GUID).bytes_le + struct.pack('<HH', 24, 1) + bytes(255))
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            fwx.Img().sections(buf, 'test', {})

if __name__ == '__main__':
    unittest.main()
