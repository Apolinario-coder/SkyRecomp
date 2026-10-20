import os
import sys
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

import shutil

# Choose compiler: clang from MSYS2, system clang, local zig, or system zig
CC = r"C:\msys64\ucrt64\bin\clang.exe"
if os.path.exists(CC):
    CC_ARGS = []
elif shutil.which("clang"):
    CC = "clang"
    CC_ARGS = []
elif os.path.exists(os.path.abspath("zig/zig.exe")):
    CC = os.path.abspath("zig/zig.exe")
    CC_ARGS = ["cc"]
elif shutil.which("zig"):
    CC = "zig"
    CC_ARGS = ["cc"]
else:
    CC = "clang"
    CC_ARGS = []

INC = os.path.abspath("gamelib-src")
BUILD_DIR = os.path.abspath("build")
OBJ_DIR = os.path.join(BUILD_DIR, "ssh_obj")
OUT_DLL = os.path.abspath("extracted/skygame.dll")
IMPLIB = os.path.abspath("skyrecomp.dll.a")

# Matching GAMELIB_ID for skyrecomp.exe
GAMELIB_ID = "5e5bafbb450f894b"

os.makedirs(OBJ_DIR, exist_ok=True)
CACHE_ID = os.path.join(OBJ_DIR, 'build.id')
try:
    with open(CACHE_ID) as f:
        CACHE_VALID = f.read().strip() == GAMELIB_ID
except FileNotFoundError:
    CACHE_VALID = False

BASE_CFLAGS = [
    "-fno-strict-aliasing",
    "-Wno-return-type",
    "-Wno-unused-label",
    "-fvisibility=hidden",
    "-march=x86-64-v3",
    "-D_FILE_OFFSET_BITS=64",
    f"-DGAMELIB_ID=\"{GAMELIB_ID}\"",
    f"-I{INC}",
]

def compile_unit(src, obj):
    if CACHE_VALID and os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
        return True, src, None, 0.0
    
    t0 = time.time()
    sz = os.path.getsize(src)
    
    # For large files (> 500KB), use -O0 to prevent compiler optimizer from hanging
    opt_level = "-O0" if sz > 500 * 1024 else "-O1"
    
    cmd = [CC] + CC_ARGS + ["-c", src, "-o", obj, opt_level] + BASE_CFLAGS
    res = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.time() - t0
    if res.returncode != 0:
        return False, src, res.stderr, dt
    return True, src, None, dt

def main():
    # skygame.dll linked against the .exe's import library: Windows only. Linux loads skygame.so, macOS skygame.dylib.
    if sys.platform != "win32":
        sys.exit("build_game.py builds the Windows skygame.dll only. On Linux and macOS the start screen builds the "
                 "game code (Set up game files); from a source checkout: make gen && make")
    print("==================================================")
    print("  Skyward Sword HD PC Recompilation Builder")
    print("==================================================")
    print(f"Compiler: {CC}")
    print(f"Target DLL: {OUT_DLL}")
    print(f"GAMELIB_ID: {GAMELIB_ID}")
    
    sources = []
    
    # 1. gamelib_entry.c
    entry_src = os.path.abspath("gamelib-src/gamelib_entry.c")
    entry_obj = os.path.join(OBJ_DIR, "gamelib_entry.o")
    sources.append((entry_src, entry_obj))
    
    # 2. Check if recompiled C code exists in ssh_generated; generate if missing
    modules = ["main", "sdk", "subsdk0", "subsdk1"]
    missing_mods = []
    for m in modules:
        m_dir = os.path.join("ssh_generated", m)
        stamp = os.path.join(m_dir, 'build.id')
        try:
            with open(stamp) as f:
                current = f.read().strip() == GAMELIB_ID
        except FileNotFoundError:
            current = False
        if not current or not os.path.exists(m_dir) or not any(f.endswith(".c") for f in os.listdir(m_dir)):
            missing_mods.append(m)

    if missing_mods:
        print("==================================================")
        print("  Generating Recompiled C Code from Game Binaries")
        print("==================================================")
        exefs_dir = os.path.abspath("extracted/exefs")
        if not os.path.exists(os.path.join(exefs_dir, "main")):
            print(f"[ERROR] Game binaries (exefs) not found in {exefs_dir}!")
            print("Please run extract_nsp.py first to extract the ROM.")
            sys.exit(1)

        gen_script = os.path.abspath("tools/gen.py")
        hle_file = os.path.abspath("runtime/hle/hle_symbols.txt")

        mod_configs = {
            "main": ("0x7000000000", []),
            "sdk": ("0x7030000000", ["--hle", hle_file]),
            "subsdk0": ("0x7032000000", []),
            "subsdk1": ("0x7034000000", []),
        }

        def run_gen(mod):
            base_addr, extra = mod_configs[mod]
            nso_path = os.path.join(exefs_dir, mod)
            out_mod_dir = os.path.abspath(os.path.join("ssh_generated", mod))
            os.makedirs(out_mod_dir, exist_ok=True)
            cmd = [sys.executable, gen_script, mod, nso_path, base_addr, out_mod_dir] + extra
            print(f"Generating C code for module '{mod}'...")
            t_m0 = time.time()
            res = subprocess.run(cmd, cwd=os.path.abspath("tools"), capture_output=True, text=True)
            if res.returncode != 0:
                print(f"[ERROR] gen.py failed for {mod}:\n{res.stderr}")
                return False, mod, res.stderr
            with open(os.path.join(out_mod_dir, 'build.id'), 'w') as f:
                f.write(GAMELIB_ID + '\n')
            print(f"[OK] Module '{mod}' generated successfully in {time.time() - t_m0:.1f}s")
            return True, mod, None

        with ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 4)) as pool:
            futures = [pool.submit(run_gen, m) for m in missing_mods]
            for fut in as_completed(futures):
                ok, mod, err = fut.result()
                if not ok:
                    print(f"[FATAL] Code generation failed for {mod}!")
                    sys.exit(1)
        print("All code generation complete!\n")

    # 3. All generated module C files
    for mod in modules:
        mod_dir = os.path.join("ssh_generated", mod)
        if not os.path.exists(mod_dir):
            print(f"[WARN] Directory not found: {mod_dir}")
            continue
        for f in os.listdir(mod_dir):
            if f.endswith(".c"):
                src_path = os.path.abspath(os.path.join(mod_dir, f))
                stem = f[:-2]
                obj_path = os.path.join(OBJ_DIR, f"{mod}_{stem}.o")
                sources.append((src_path, obj_path))
                
    total = len(sources)
    print(f"Total C source translation units: {total}")
    
    # Check how many need compilation
    to_compile = [s for s, o in sources if not CACHE_VALID or not os.path.exists(o) or os.path.getmtime(o) < os.path.getmtime(s)]
    print(f"Units needing compilation: {len(to_compile)} (cached: {total - len(to_compile)})")
    
    # Sort largest files first so they start immediately on all worker cores
    sources.sort(key=lambda pair: -os.path.getsize(pair[0]))
    
    cores = min(os.cpu_count() or 4, 16)
    print(f"Compiling with {cores} parallel threads (largest files queued first)...")
    
    t0 = time.time()
    compiled = 0
    errors = []
    
    with ThreadPoolExecutor(max_workers=cores) as executor:
        futures = {executor.submit(compile_unit, src, obj): src for src, obj in sources}
        for fut in as_completed(futures):
            ok, src, err, dt = fut.result()
            fname = os.path.basename(src)
            if not ok:
                errors.append((src, err))
                print(f"\n[ERROR] Failed to compile {fname}:\n{err}", file=sys.stderr)
            compiled += 1
            
            remaining = total - compiled
            if remaining <= 20 or compiled % 100 == 0 or compiled == total:
                elapsed = time.time() - t0
                pct = (compiled / total) * 100
                speed = compiled / elapsed if elapsed > 0 else 0
                if dt > 1.0:
                    print(f"Progress: {compiled}/{total} ({pct:.1f}%) [finished {fname} in {dt:.1f}s - {remaining} left]", flush=True)
                else:
                    print(f"Progress: {compiled}/{total} ({pct:.1f}%) [{speed:.1f} files/s - {remaining} left]", flush=True)
                
    if errors:
        print(f"\n[FATAL] Compilation failed with {len(errors)} error(s).", file=sys.stderr)
        sys.exit(1)
        
    print(f"\nAll {total} objects compiled successfully in {time.time() - t0:.1f}s!")
    print(f"Linking {OUT_DLL} with {IMPLIB}...")
    
    link_cmd = [CC] + CC_ARGS + ["-shared", "-fuse-ld=lld", "-o", OUT_DLL]
    rsp_path = os.path.join(OBJ_DIR, "objects.rsp")
    with open(rsp_path, "w") as rf:
        for _, obj in sources:
            rf.write(f'"{obj.replace(chr(92), "/")}"\n')
        rf.write(f'"{IMPLIB.replace(chr(92), "/")}"\n')
        
    link_cmd.append(f"@{rsp_path}")
    t_link = time.time()
    lres = subprocess.run(link_cmd, capture_output=True, text=True)
    if lres.returncode != 0:
        print(f"[LINK ERROR]:\n{lres.stderr}", file=sys.stderr)
        sys.exit(1)
        
    # Write matching ID file
    with open(OUT_DLL + ".id", "w") as f:
        f.write(f"{GAMELIB_ID}\n")
    with open(CACHE_ID, 'w') as f:
        f.write(GAMELIB_ID + '\n')
        
    print(f"Linked {OUT_DLL} successfully in {time.time() - t_link:.1f}s!")
    print(f"Native Game Library size: {os.path.getsize(OUT_DLL) / (1024*1024):.2f} MB")
    print("Build complete! You can now launch the game with skyrecomp.exe")

if __name__ == "__main__":
    main()
