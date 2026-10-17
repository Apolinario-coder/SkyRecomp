# PyInstaller spec: the seed generator (tools/rando/las_headless.py + tools/rando + vendored LAS-Randomizer) as a
# standalone onedir helper, so players without Python can generate seeds. `make rando-helper` -> build/helper/rando-gen/
# (rando-gen[.exe] + _internal/). Same command line as las_headless.py: rando-gen <romfs> <out dir> <request.json>.
# Inside the bundle (sys._MEIPASS) the source layout is kept: tools/rando's data files at the root (modules find them
# via __file__), LAS's RandomizerCore/{Data,ASM/Patches} and Resources/ beside them (randomizer_paths.py, argv[0]).
import ctypes.util
import glob
import os
import shutil
import sys

from PyInstaller.utils.hooks import collect_dynamic_libs, collect_submodules

ROOT = os.path.normpath(os.path.join(SPECPATH, '..', '..'))
RANDO = os.path.join(ROOT, 'tools', 'rando')
LAS = os.path.join(ROOT, 'third_party', 'LAS-Randomizer')

datas = [(f, '.') for e in ('json', 'yml', 'txt') for f in glob.glob(os.path.join(RANDO, '*.' + e))]
datas += [(os.path.join(LAS, 'RandomizerCore', 'Data'), 'RandomizerCore/Data'),
          (os.path.join(LAS, 'RandomizerCore', 'ASM', 'Patches'), 'RandomizerCore/ASM/Patches'),
          (os.path.join(LAS, 'Resources'), 'Resources')]

# keystone loads its library with ctypes from its package dir: the Windows wheel ships keystone.dll there; on macOS
# the wheel has none and the library comes from the system (Homebrew `keystone`), so take that one along
binaries = collect_dynamic_libs('keystone', destdir='keystone')
if not binaries:
    # KEYSTONE_LIB (Makefile: `make deps`' build for the app's minimum macOS) first, else the system's
    lib = os.environ.get('KEYSTONE_LIB') or ctypes.util.find_library('keystone')
    if not lib:
        for d in ('/opt/homebrew/lib', '/usr/local/lib'):
            if os.path.exists(os.path.join(d, 'libkeystone.dylib')):
                lib = os.path.join(d, 'libkeystone.dylib')
    if not lib:
        sys.exit('rando_gen.spec: libkeystone not found (macOS: brew install keystone)')
    # under the name keystone.py looks for (Homebrew's resolves to libkeystone.0.dylib)
    os.makedirs(workpath, exist_ok=True)
    shutil.copyfile(os.path.realpath(lib), os.path.join(workpath, 'libkeystone.dylib'))
    binaries = [(os.path.join(workpath, 'libkeystone.dylib'), 'keystone')]

a = Analysis(
    [os.path.join(RANDO, 'las_headless.py')],
    pathex=[RANDO, LAS],
    binaries=binaries,
    datas=datas,
    hiddenimports=collect_submodules('RandomizerCore'),
    excludes=['tkinter', 'PySide6', 'shiboken6', 'cx_Freeze', 'pydoc', 'doctest', 'unittest', 'pdb', 'IPython',
              'numpy', 'PIL.ImageQt', 'PIL.ImageTk', 'setuptools', '_distutils_hack', 'pkg_resources',
              'ssl', '_ssl',
              # optional stdlib natives the generator never uses (size): OpenSSL hashes (hashlib falls back to its
              # built-in ones), CJK codecs, decimal
              '_hashlib', '_multibytecodec', '_codecs_cn', '_codecs_hk', '_codecs_iso2022', '_codecs_jp', '_codecs_kr',
              '_codecs_tw', '_decimal'],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    # same seed ID -> same seed: set iteration order must not vary between runs (fixed in the bootloader
    # rather than left to PYTHONHASHSEED in the environment)
    [('hash_seed=0', None, 'OPTION')],
    exclude_binaries=True,
    name='rando-gen',
    console=True,  # stdout = the `stage` progress lines the runtime reads (no window: CREATE_NO_WINDOW)
    strip=sys.platform == 'darwin',
    upx=False,
)
coll = COLLECT(exe, a.binaries, a.datas, strip=sys.platform == 'darwin', upx=False, name='rando-gen')
