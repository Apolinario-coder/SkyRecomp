"""Source-tree checks; no game files, network or third-party Python packages needed."""
import ast
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SourceTreeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0')[:-1]

    def test_game_files_and_generated_binaries_are_not_tracked(self):
        bad = []
        for rel in self.files:
            p = Path(rel)
            if p.suffix.lower() in {'.keys', '.nsp', '.xci', '.nca', '.tik'}:
                bad.append(rel)
            elif p.suffix.lower() in {'.exe', '.dll', '.a', '.lib'} and (len(p.parts) == 1 or p.parts[0] == 'tools'):
                bad.append(rel)
        self.assertEqual(bad, [], 'Distribute generated programs through release packages, not the source tree')

    def test_no_literal_decryption_keys_in_python_sources(self):
        bad = []
        for rel in self.files:
            if not rel.endswith('.py') or not (ROOT / rel).is_file():
                continue
            tree = ast.parse((ROOT / rel).read_text(encoding='utf-8-sig'), filename=rel)
            for node in ast.walk(tree):
                if not isinstance(node, (ast.Assign, ast.AnnAssign)):
                    continue
                targets = node.targets if isinstance(node, ast.Assign) else [node.target]
                names = [t.id.lower() for t in targets if isinstance(t, ast.Name)]
                if not any(re.search(r'(title|header|master|key_area).*key|^titlekey$', n) for n in names):
                    continue
                value = node.value
                if isinstance(value, ast.Call) and value.args:
                    value = value.args[0]
                if isinstance(value, ast.Constant) and isinstance(value.value, str) and re.fullmatch(r'[0-9a-fA-F]{32}|[0-9a-fA-F]{64}', value.value):
                    bad.append(f'{rel}:{node.lineno}')
        self.assertEqual(bad, [], 'Decryption keys must never be embedded in source code')

    def test_local_data_and_duplicate_resources_are_not_tracked(self):
        local_dirs = {'build', 'cache', 'save', 'extracted', 'Rom Game', 'mods', 'gamelib-src', 'ssh_generated'}
        root_copies = {'icon.png', 'Baloo2-SemiBold.ttf', 'Baloo2-OFL.txt'}
        bad = [rel for rel in self.files if Path(rel).parts[0] in local_dirs or rel in root_copies]
        self.assertEqual(bad, [], 'Keep local data out of Git and maintain resources at their canonical source paths')

    def test_retired_extractor_fails_without_reading_keys_or_creating_output(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/extract_nsp.py'), 'missing.nsp', 'missing.keys'],
                                capture_output=True, text=True, cwd=ROOT)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('retired', result.stderr)
        self.assertIn('prepared ExeFS/RomFS folder', result.stderr)


if __name__ == '__main__':
    unittest.main()
