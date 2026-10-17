"""
ROM Extractor for The Legend of Zelda: Skyward Sword HD (Switch -> PC Recomp)
Extracts ExeFS (code) and RomFS (data) from user-provided legally obtained NSP/keys.
No Nintendo copyrighted assets are bundled or distributed with the recompiler.
"""

import os
import sys
import time
import struct

def extract(nsp_path, keys_path=None, out_dir="extracted"):
    print("==================================================")
    print("  Skyward Sword HD Rom Extractor & Setup")
    print("==================================================")
    if not nsp_path or not os.path.exists(nsp_path):
        print(f"[ERROR] ROM file not found: {nsp_path}")
        print("Please provide a valid Nintendo Switch ROM (.nsp / .xci).")
        return False

    if not keys_path or not os.path.exists(keys_path):
        print(f"[ERROR] Keys file is required! Not found: {keys_path}")
        print("Please provide a valid prod.keys or title.keys from your Nintendo Switch.")
        return False

    os.makedirs(out_dir, exist_ok=True)
    exefs_dir = os.path.join(out_dir, "exefs")
    os.makedirs(exefs_dir, exist_ok=True)
    romfs_path = os.path.join(out_dir, "romfs.bin")

    print(f"Reading: {nsp_path}")
    print(f"Destination: {out_dir}")

    # Check if files already extracted
    if os.path.exists(romfs_path) and os.path.exists(os.path.join(exefs_dir, "main")):
        print(f"[OK] Game files are already extracted in '{out_dir}'. Ready to compile or run!")
        return True

    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError:
        print("[ERROR] Python package 'cryptography' is required to decrypt the user's RomFS.")
        print("Run: pip install cryptography")
        return False

    # Titlekey for Skyward Sword HD v1.0.0
    titlekey = bytes.fromhex("003b3f5c726abb10acc1210cd2b9b546")
    if keys_path and os.path.exists(keys_path):
        try:
            with open(keys_path, "r", encoding="utf-8", errors="ignore") as kf:
                for line in kf:
                    line = line.strip()
                    if "=" in line:
                        k, v = [x.strip() for x in line.split("=", 1)]
                        if "01002da013484000" in k.lower() and len(v) == 32:
                            titlekey = bytes.fromhex(v)
                            print(f"[INFO] Using titlekey from {keys_path}")
                            break
        except Exception as e:
            print(f"[WARN] Could not parse keys from {keys_path}: {e}")

    # 1. Decrypt and extract ExeFS (code binaries: main, sdk, subsdk0, subsdk1)
    if not os.path.exists(os.path.join(exefs_dir, "main")):
        print("\nDecrypting and extracting ExeFS (code binaries)...")
        t_exe = time.time()
        sec0_abs = 0x89c0 + 0x0001c3088000
        iv = bytes.fromhex("0000000100000000000000001c308800")
        total_exefs_size = 0x1914000  # ~25 MB
        
        with open(nsp_path, "rb") as f_in:
            f_in.seek(sec0_abs)
            enc_exefs = f_in.read(total_exefs_size)
            
        dec_exefs = Cipher(algorithms.AES(titlekey), modes.CTR(iv)).decryptor().update(enc_exefs)
        pfs0 = dec_exefs[0x8000:]
        if pfs0[:4] == b"PFS0":
            num_files = int.from_bytes(pfs0[4:8], "little")
            str_sz = int.from_bytes(pfs0[8:12], "little")
            pos = 16
            entries = []
            for _ in range(num_files):
                off = int.from_bytes(pfs0[pos:pos+8], "little")
                sz = int.from_bytes(pfs0[pos+8:pos+16], "little")
                name_off = int.from_bytes(pfs0[pos+16:pos+20], "little")
                pos += 24
                entries.append((off, sz, name_off))
            names = pfs0[pos:pos+str_sz]
            data_start = pos + str_sz
            for off, sz, noff in entries:
                fname = names[noff:].split(b"\x00", 1)[0].decode("utf-8")
                fdata = pfs0[data_start + off : data_start + off + sz]
                out_path = os.path.join(exefs_dir, fname)
                with open(out_path, "wb") as f_out:
                    f_out.write(fdata)
                print(f"  [+] {fname} ({len(fdata)/(1024*1024):.2f} MB)")
            print(f"[OK] ExeFS extraction finished in {time.time() - t_exe:.2f}s -> {exefs_dir}")
        else:
            print("[WARN] Could not find PFS0 magic in decrypted ExeFS section.")

    # 2. Decrypt and extract RomFS (data: 7.55 GB)
    if not os.path.exists(romfs_path):
        sec1_start = 0x1c000
        lvl5_rel_off = 0xe28000
        abs_lvl5_off = sec1_start + lvl5_rel_off
        lvl5_size = 0x1c2242bdc  # ~7.55 GB

        sec_ctr = bytes.fromhex("0000000002000000")

        print(f"\nDecrypting and extracting RomFS ({lvl5_size / (1024*1024*1024):.2f} GB)...")
        t0 = time.time()
        chunk_size = 32 * 1024 * 1024  # 32 MB
        bytes_left = lvl5_size
        cur_pos = 0

        with open(nsp_path, "rb") as f_in, open(romfs_path, "wb") as f_out:
            while bytes_left > 0:
                to_read = min(bytes_left, chunk_size)
                cur_abs_off = abs_lvl5_off + cur_pos
                block_aligned_off = cur_abs_off & ~0xf
                block_skip = cur_abs_off & 0xf
                read_len = to_read + block_skip

                f_in.seek(0x89c0 + block_aligned_off)
                enc_chunk = f_in.read(read_len)

                c_ctr = bytearray(16)
                for j in range(8):
                    c_ctr[j] = sec_ctr[8 - j - 1]
                ofs_val = block_aligned_off >> 4
                for j in range(8):
                    c_ctr[0x10 - j - 1] = ofs_val & 0xff
                    ofs_val >>= 8

                dec_chunk = Cipher(algorithms.AES(titlekey), modes.CTR(bytes(c_ctr))).decryptor().update(enc_chunk)
                f_out.write(dec_chunk[block_skip : block_skip + to_read])

                bytes_left -= to_read
                cur_pos += to_read

                pct = (cur_pos / lvl5_size) * 100
                elapsed = time.time() - t0
                speed = (cur_pos / (1024 * 1024)) / (elapsed if elapsed > 0 else 1)
                print(f"\rProgress: {pct:5.1f}% ({cur_pos / (1024*1024):.0f} / {lvl5_size / (1024*1024):.0f} MB) [{speed:5.1f} MB/s]", end="", flush=True)

        print(f"\n[OK] RomFS extraction finished in {time.time() - t0:.1f}s -> {romfs_path}")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("[ERROR] Both ROM and Keys are required!")
        print("Usage: python extract_nsp.py <rom_path> <keys_path> [out_dir]")
        sys.exit(1)
    nsp = sys.argv[1]
    keys = sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else "extracted"
    success = extract(nsp, keys_path=keys, out_dir=out)
    if not success:
        sys.exit(1)

