"""Run prescale_verify over many frames of many clips.

One IDENTICAL frame is not evidence; a bake that is right on frame 0 and wrong
on frame 7 would show up as jitter on the device, not as a broken still. This
dumps every frame of the requested clips, bakes nothing itself, and calls
host/prescale_verify.exe for each pair.

    python tools/prescale_verify_all.py [--pack NAME] [--clips a,b] [--frames N]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PY = r'D:\Espressif\LittleWorld\python\python.exe'
VERIFY = ROOT / 'build' / 'host' / 'PrescaleVerify.exe'
OUT = ROOT / 'build' / 'v45-assets'

# canvas, scale, and where each pack lands on screen, from the firmware callers:
#   lw_os.c / lw_rpg_view.c create the 256x256 characters at scale 288
#   lw_app_pet.c creates the 320x320 couple at scale 282 and the dog at 256
PACKS = {
    'male': {'src': 'assets/visual-v4-2/runtime/male.lwp', 'scale': 288,
             'clips': ['idle', 'blink', 'wave', 'walk_down', 'walk_left',
                       'walk_right', 'walk_up']},
    'qianqian-base': {'src': 'dist/littleworld-v4-sd-private/littleworld/qianqian-base.lwp',
                      'scale': 288,
                      'clips': ['idle', 'sleep', 'sleep_enter', 'sleep_idle',
                                'sleep_turn', 'walk_down', 'walk_left',
                                'walk_right', 'walk_up']},
    'qianqian-wave': {'src': 'dist/littleworld-v4-sd/littleworld/qianqian-wave.lwp',
                      'scale': 288, 'clips': ['wave']},
    'daily': {'src': 'dist/littleworld-v4-sd/littleworld/daily.lwp', 'scale': 282,
              'clips': ['couple.seated', 'couple.wave', 'couple.rest',
                        'dog.idle', 'dog.sleep']},
}


def run(cmd):
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit('%s failed:\n%s\n%s' % (cmd[0], out.stdout, out.stderr))
    return out.stdout


def clip_table(pack):
    raw = Path(pack).read_bytes()
    import struct
    HEADER = struct.Struct('<8sHHHH')
    CLIP = struct.Struct('<32sHhhBBI32H')
    _magic, w, h, count, _kind = HEADER.unpack_from(raw, 0)
    table = {}
    for i in range(count):
        rec = CLIP.unpack_from(raw, 16 + 108 * i)
        table[rec[0].split(b'\0')[0].decode()] = {'frames': rec[1],
                                                  'anchor_x': rec[2],
                                                  'anchor_y': rec[3]}
    return w, h, table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', action='append', default=None)
    ap.add_argument('--frames', type=int, default=0,
                    help='0 = every frame (slow but complete)')
    ap.add_argument('--json', type=Path,
                    default=ROOT / 'reports' / 'perf-v4-5' / 'prescale_verify.json')
    args = ap.parse_args()

    names = args.pack or list(PACKS)
    rows = []
    failures = 0
    for name in names:
        spec = PACKS[name]
        src = ROOT / spec['src']
        baked = OUT / ('%s.lwp' % name)
        if not baked.exists():
            run([PY, 'tools/lwp_prescale.py', str(src), str(baked),
                 '--scale', str(spec['scale'])])
        bw, bh, btable = clip_table(baked)
        sw, sh, stable = clip_table(src)
        for clip in spec['clips']:
            if clip not in stable or clip not in btable:
                print('skip %s %s (missing)' % (name, clip))
                continue
            count = stable[clip]['frames']
            if args.frames:
                count = min(count, args.frames)
            for frame in range(count):
                a = OUT / ('%s-%s-%02d-src.raw' % (name, clip, frame))
                b = OUT / ('%s-%s-%02d-baked.raw' % (name, clip, frame))
                run([PY, 'tools/lwp_frame_dump.py', str(src), clip,
                     '--frame', str(frame), '-o', str(a)])
                run([PY, 'tools/lwp_frame_dump.py', str(baked), clip,
                     '--frame', str(frame), '-o', str(b)])
                cmd = [str(VERIFY), str(a), str(sw), str(sh), str(spec['scale']),
                       str(b), str(bw), str(bh),
                       str(stable[clip]['anchor_x']), str(stable[clip]['anchor_y']),
                       str(btable[clip]['anchor_x']), str(btable[clip]['anchor_y'])]
                res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
                diff = 0
                worst = 0
                for line in res.stdout.splitlines():
                    if line.startswith('differing pixels:'):
                        diff = int(line.split()[2])
                    elif line.startswith('worst channel delta:'):
                        worst = int(line.split()[3].rstrip(','))
                ok = res.returncode == 0
                if not ok:
                    failures += 1
                rows.append({'pack': name, 'clip': clip, 'frame': frame,
                             'identical': ok, 'diff_pixels': diff,
                             'worst_delta': worst,
                             'src_canvas': [sw, sh], 'baked_canvas': [bw, bh],
                             'scale': spec['scale']})
                print('%-14s %-14s frame %2d  %s%s'
                      % (name, clip, frame, 'IDENTICAL' if ok else 'DIFFERS',
                         '' if ok else '  %d px, worst %d' % (diff, worst)))

    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps({'results': rows,
                                     'frames_checked': len(rows),
                                     'failures': failures}, indent=2))
    print('\n%d frames checked, %d differ' % (len(rows), failures))
    print('json: %s' % args.json)
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
