"""Check the Windows generator compatibility patch without game files."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


class GeneratorHeaderTest(unittest.TestCase):
    def test_only_cfg_is_cleared_and_repeated_runs_are_safe(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generator.exe'
            original = bytearray(512)
            struct.pack_into('<I', original, 0x3c, 0x80)
            original[0x80:0x84] = b'PE\0\0'
            struct.pack_into('<H', original, 0x80 + 24, 0x20b)
            offset = 0x80 + 24 + 70
            struct.pack_into('<H', original, offset, 0xc160)
            path.write_bytes(original)
            patch = Path(__file__).with_name('pe_no_cfg.py')
            expected = bytearray(original)
            struct.pack_into('<H', expected, offset, 0x8160)
            for _ in range(2):
                subprocess.run([sys.executable, str(patch), str(path)], check=True, capture_output=True)
                self.assertEqual(path.read_bytes(), expected)


if __name__ == '__main__':
    unittest.main()
