"""Presented PNGs must retain displayed RGB even when framebuffer alpha is zero."""
import base64
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from native_debug import save_png


class Screenshot(unittest.TestCase):
    def test_ignored_alpha_does_not_become_document_transparency(self):
        pixels = bytes([10, 20, 30, 0, 40, 50, 60, 91, 70, 80, 90, 217, 100, 110, 120, 255])
        result = dict(ok=True, surface='presented', format='rgba8', width=2, height=2,
                      data=base64.b64encode(pixels).decode())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'screen.png'
            save_png(result, path)
            data = path.read_bytes()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(struct.unpack('>IIBBBBB', data[16:29]), (2, 2, 8, 2, 0, 0, 0))
        offset, compressed = 8, b''
        while offset < len(data):
            length = struct.unpack('>I', data[offset:offset+4])[0]
            kind = data[offset+4:offset+8]
            payload = data[offset+8:offset+8+length]
            if kind == b'IDAT': compressed += payload
            offset += length+12
        self.assertEqual(zlib.decompress(compressed), bytes([0, 10, 20, 30, 40, 50, 60, 0, 70, 80, 90, 100, 110, 120]))


if __name__ == '__main__':
    unittest.main()
