#!/usr/bin/env python3
"""Performance sweep: warp to every room in the game and record frame stats.

  tools/perf_sweep.py run      warp through build/sweep_targets.json in batches (one game instance per batch,
                               fresh scratch save), logs in build/sweep/batch_NN.log
  tools/perf_sweep.py report   per-room table -> build/sweep/report.txt (rooms under 58 fps or with long frames)

Each room gets STEP frames: the first SETTLE frames (load, fade) are ignored, the rest are averaged from the
LARECOMP_FPS_INTERVAL=0.5 "[metal]" lines (fps, max frame ms, gpu ms, passes, fbcopy).
"""
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'build', 'sweep')
FIRST, STEP, SETTLE, BATCH = 2700, 360, 150, 40
TITLE_INPUT = '400:A:6,520:A:6,900:ZL:8,900:ZR:8,1300:A:6,1600:A:6,1900:A:6,2200:A:6'


def run():
    """Warp through all targets; one game instance per batch. A crash (FATAL / signal) ends that instance at once:
    the crashing target is recorded in crashes.json and the batch resumes from the next target."""
    targets = json.load(open(os.path.join(ROOT, 'build', 'sweep_targets.json')))
    os.makedirs(OUT, exist_ok=True)
    crash_file = os.path.join(OUT, 'crashes.json')
    crashes = json.load(open(crash_file)) if os.path.exists(crash_file) else []
    done = set()
    for f in os.listdir(OUT):
        if f.endswith('.json') and f.startswith('batch_'):
            log = os.path.join(OUT, f[:-5] + '.log')
            if os.path.exists(log):
                for i, t in enumerate(json.load(open(os.path.join(OUT, f)))):
                    done.add(tuple(t))  # rerun policy: a batch file means those targets were attempted
    todo = [t for t in targets if tuple(t) not in done and [*t] not in [c[:2] for c in crashes]]
    n = len([f for f in os.listdir(OUT) if f.startswith('batch_') and f.endswith('.json')])
    while todo:
        chunk = todo[:BATCH]
        log = os.path.join(OUT, f'batch_{n:02d}.log')
        warps = ','.join(f'{FIRST + i * STEP}:{lv}:{loc}' for i, (lv, loc) in enumerate(chunk))
        subprocess.run([os.path.join(ROOT, 'tools', 'fresh_save.sh'), 'testsave_sweep'], cwd=ROOT,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        env = dict(os.environ, LARECOMP_SAVE_DIR='build/testsave_sweep', LARECOMP_INPUT=TITLE_INPUT,
                   LARECOMP_WARP_AT=warps, LARECOMP_FPS_INTERVAL='0.5', LARECOMP_WINDOW='1280x720')
        end = FIRST + len(chunk) * STEP + 60
        crashed_at = None
        with open(log, 'w') as f:
            p = subprocess.Popen([os.path.join(ROOT, 'build', 'lasrecomp'), 'extracted'], cwd=ROOT, env=env,
                                 stdout=f, stderr=subprocess.STDOUT)
            t0 = time.time()
            while p.poll() is None and time.time() - t0 < end / 60 * 1.6 + 120:
                time.sleep(3)
                text = open(log, errors='replace').read()
                if 'FATAL' in text or '*** signal' in text:
                    jumps = re.findall(r'jumping to (\S+) / (\S+)', text)
                    crashed_at = len(jumps) - 1
                    break
                fr = [int(m) for m in re.findall(r'\] t=[\d.]+ frame (\d+)', text[-4000:])]
                if fr and fr[-1] >= end:
                    break
            if p.poll() is None:
                p.kill()
                p.wait(10)
        if crashed_at is not None and 0 <= crashed_at < len(chunk):
            crashes.append(list(chunk[crashed_at]) + [os.path.basename(log)])
            json.dump(crashes, open(crash_file, 'w'), indent=1)
            chunk = chunk[:crashed_at]  # targets measured before the crash
            print(f'batch {n}: crash at {crashes[-1][:2]}', flush=True)
            todo = todo[crashed_at + 1:]
        else:
            todo = todo[len(chunk):]
        json.dump(chunk, open(os.path.join(OUT, f'batch_{n:02d}.json'), 'w'))
        print(f'batch {n}: {len(chunk)} rooms measured', flush=True)
        n += 1


LINE = re.compile(r'\[metal\] t=[\d.]+ frame (\d+) ([\d.]+) fps max (\d+)ms level (\S+) \((\d+) draws, \d+ skipped, '
                  r'(\d+) passes.*?gpu (\d+)ms.*?fbcopy (\d+)')


def report():
    rows = []
    n = 0
    while os.path.exists(os.path.join(OUT, f'batch_{n:02d}.json')):
        chunk = json.load(open(os.path.join(OUT, f'batch_{n:02d}.json')))
        text = open(os.path.join(OUT, f'batch_{n:02d}.log'), errors='replace').read()
        crashed = 'signal ' in text
        lines = [(int(m[0]), float(m[1]), int(m[2]), m[3], int(m[5]), int(m[6]), int(m[7])) for m in LINE.findall(text)]
        for i, (lv, loc) in enumerate(chunk):
            a, b = FIRST + i * STEP + SETTLE, FIRST + (i + 1) * STEP - 30  # end before the next warp (its unload hitch)
            s = [l for l in lines if a <= l[0] < b]
            if not s:
                rows.append((lv, loc, None, None, None, None, None, 'no data' + (' (crash)' if crashed else '')))
                continue
            fps = min(l[1] for l in s)
            mx = max(l[2] for l in s)
            per = lambda k: sum(l[k] for l in s) / sum(max(l[1] * 0.5, 1) for l in s)  # per frame
            level = s[-1][3]
            note = '' if level == lv else f'(level {level})'
            rows.append((lv, loc, fps, mx, per(4), per(5), per(6), note))
        n += 1
    rows.sort(key=lambda r: (r[2] is None, r[2] if r[2] is not None else 0))
    with open(os.path.join(OUT, 'report.txt'), 'w') as f:
        f.write(f'{"level":22} {"locator":34} {"minfps":>6} {"maxms":>5} {"passes/f":>8} {"gpu/f":>6} {"fbcopy/f":>8}\n')
        for lv, loc, fps, mx, pa, gp, fb, note in rows:
            if fps is None:
                f.write(f'{lv:22} {loc:34} {note}\n')
            else:
                f.write(f'{lv:22} {loc:34} {fps:6.1f} {mx:5d} {pa:8.1f} {gp:6.1f} {fb:8.1f} {note}\n')
    slow = [r for r in rows if r[2] is not None and (r[2] < 58 or r[3] > 25)]
    print(f'{len(rows)} rooms, {len(slow)} slow or hitchy, {sum(1 for r in rows if r[2] is None)} without data')


if __name__ == '__main__':
    {'run': run, 'report': report}[sys.argv[1] if len(sys.argv) > 1 else 'run']()
