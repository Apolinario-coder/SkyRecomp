"""A new generator version must not reuse objects from an older build."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location('builder', Path(__file__).resolve().parents[1] / 'build_game.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BuildCacheTest(unittest.TestCase):
    def test_generator_identity_invalidates_cached_objects(self):
        with tempfile.TemporaryDirectory() as d:
            src, obj = Path(d) / 'unit.c', Path(d) / 'unit.o'
            src.write_text('void unit(void) {}')
            obj.write_bytes(b'old object')
            for valid in (False, True):
                with self.subTest(valid=valid), patch.object(builder, 'CACHE_VALID', valid), patch.object(
                        builder.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
                    self.assertTrue(builder.compile_unit(str(src), str(obj))[0])
                    self.assertEqual(run.called, not valid)


if __name__ == '__main__':
    unittest.main()
