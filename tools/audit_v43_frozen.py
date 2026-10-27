"""Audit the frozen V4.3 bytes; optionally copy a local, immutable DSH handoff.

Never mounts a card, invokes a generation service, or edits the DSH checkout.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FROZEN = '2900569e381f1e06ecf88d720a3f032a9bd97db6'
OUT = ROOT / 'reports/v43-integration-support'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pack(path, prefix):
    data = path.read_bytes()
    magic, w, h, count, reserved = struct.unpack_from('<8sHHHH', data)
    assert magic == b'LWPX0001' and not reserved
    assert 0 < w <= 1024 and 0 < h <= 600 and w*h*3 <= 1250000
    assert 0 < count <= 32
    clips, ranges = [], []
    for i in range(count):
        rec = data[16+i*108:16+(i+1)*108]
        assert len(rec) == 108
        raw, frames, ax, ay, loop, pad, offset, *duration = struct.unpack('<32sHhhBBI32H', rec)
        assert b'\0' in raw
        name = raw.split(b'\0')[0].decode('ascii')
        assert name and 0 < frames <= 32 and 0 <= ax <= w and 0 <= ay <= h
        assert loop <= 1 and pad == 0 and all(20 <= t <= 2000 for t in duration[:frames])
        end = offset+frames*w*h*3
        assert offset >= 16+count*108 and end <= len(data)
        assert all(end <= a or offset >= b for a, b in ranges)
        ranges.append((offset, end))
        clips.append(dict(name=name, id=prefix+'.'+name, frames=frames,
                          anchor=[ax, ay], loop=bool(loop), duration_ms=duration[:frames],
                          payload_offset=offset, frame_bytes=w*h*3))
    assert len({c['id'] for c in clips}) == count
    assert max(b for _, b in ranges) == len(data)
    return dict(format='LWPX0001', resolution=[w, h], pixel_format='RGB565_LE plane + A8 plane',
                record_bytes=108, frame_bytes=w*h*3, clips=clips,
                ids=[c['id'] for c in clips], frame_total=sum(c['frames'] for c in clips))


def scene(path):
    data = path.read_bytes()
    magic, w, h, pad = struct.unpack_from('<8sHHI', data)
    assert magic == b'LWSR0001' and pad == 0
    assert (w, h) in ((1024, 600), (256, 150)) and len(data) == 16+w*h*2
    return dict(format='LWSR0001', resolution=[w, h], pixel_format='RGB565_LE',
                ids=[], frame_bytes=w*h*2)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle', type=Path, help='Local-only new handoff directory; never a mounted card')
    p.add_argument('--include-local-qianqian', action='store_true')
    args = p.parse_args()
    entries = []

    def add(rel, target, prefix, loader, fallback, local=False):
        path = ROOT / rel
        assert path.is_file(), rel
        if not local:
            expected = subprocess.check_output(['git','rev-parse',FROZEN+':'+rel],cwd=ROOT,text=True).strip()
            actual = subprocess.check_output(['git','hash-object',str(path)],cwd=ROOT,text=True).strip()
            assert expected == actual, 'Frozen resource changed: '+rel
        info = pack(path, prefix) if prefix else scene(path)
        entries.append(dict(path=rel, target=target, bytes=path.stat().st_size,
                            sha256=sha(path), frozen_tracked=not local, local_only=local,
                            loader=loader, fallback=fallback, **info))

    add('assets/visual-v4-3/runtime/skin.lwp','skin.lwp','v43skin',
        'LWPX registry -> lw_assets -> lw_skin shared atlas lease',
        'Native LVGL controls; visual downgrade must be reported')
    add('assets/visual-v4-3/runtime/daily.lwp','daily.lwp','v42',
        'LWPX registry -> lw_assets -> lw_sprite',
        'Old polish.lwp if explicitly supplied; otherwise labelled native pet view')
    add('assets/visual-v4-2/runtime/male.lwp','male.lwp','male',
        'LWPX registry -> lw_actor_art_t[0] -> lw_sprite',
        'Embedded LWFA idle only; do not call it the full V4.3 motion set')
    add('assets/visual-v4/runtime/qianqian-wave.lwp','qianqian-wave.lwp','qianqian',
        'Append to qianqian namespace; preserve base idle',
        'Original idle retained; missing wave must not replace the character')
    add('assets/visual-v4-2/runtime/ui.lwp','ui.lwp','v42ui',
        'LWPX registry -> lw_assets -> RPG journal/dialogue/reward UI',
        'Native UI drawing')
    for i in range(3):
        for suffix in ('','-thumb'):
            name=f'scene-{i}{suffix}.lwr'
            add('assets/visual-v4/runtime/'+name,name,None,
                'lw_visual_assets_configure(root) -> lw_visual_scene_image',
                'Native geometric room / theme thumbnail')
    for name in ('rpg-room','daily-room','daily-room-day','daily-room-night'):
        add('assets/visual-v4-2/runtime/'+name+'.lwr',name+'.lwr',None,
            'lw_visual_aux_configure(root) -> lw_visual_rpg_image/lw_visual_daily_image',
            'Day/night falls back to daily-room; missing base returns native page')
    if args.include_local_qianqian:
        add('private/pixel-duo/qianqian/qianqian.lwpx','qianqian-base.lwp','qianqian',
            'Same LWPX0001 bytes under .lwp filename; append wave after this base',
            'Embedded original Qianqian idle via DSH LWFA loader', local=True)

    fonts=[]
    for size in (18,22,28,44,88):
        path=ROOT/f'firmware/components/lw_ui/fonts/lw_font_{size}.c'
        rel=path.relative_to(ROOT).as_posix()
        assert subprocess.check_output(['git','hash-object',str(path)],cwd=ROOT,text=True).strip() == subprocess.check_output(['git','rev-parse',FROZEN+':'+rel],cwd=ROOT,text=True).strip()
        fonts.append(dict(path=rel, format='LVGL 9 C bitmap font, compiled into firmware',
                          nominal_px=size, bytes=path.stat().st_size, sha256=sha(path),
                          id=f'lw_font_{size}', loader='CMake lw_ui/fonts/*.c + lw_theme',
                          fallback='No automatic alternate Chinese font; missing linked symbol is a build error'))
    ids=[i for e in entries for i in e['ids']]
    assert len(ids)==len(set(ids)) and len(ids)<=64
    result=dict(frozen_commit=FROZEN, private_data_modified=False, device_operations=0,
                paid_calls=0, resources=entries, fonts=fonts, unique_asset_ids=len(ids),
                bundle_bytes=sum(e['bytes'] for e in entries),
                ui_registry_budget_bytes=2097152, tested_host_asset_peak_bytes=1999872,
                note='This is a resource audit, not a joint-firmware or board acceptance.')
    OUT.mkdir(parents=True, exist_ok=True)
    # Public inventory excludes the optional local-only source metadata.
    public={**result,'resources':[e for e in entries if not e['local_only']]}
    public['unique_asset_ids']=sum(len(e['ids']) for e in public['resources'])
    public['bundle_bytes']=sum(e['bytes'] for e in public['resources'])
    (OUT/'RESOURCE_INVENTORY.json').write_text(json.dumps(public,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    if args.bundle:
        dest=args.bundle.resolve()
        assert dest.drive.upper()=='D:' and 'littleworld' in dest.as_posix().lower()
        dest.mkdir(parents=True,exist_ok=True)
        for e in entries:
            target=dest/'runtime'/e['target']; target.parent.mkdir(exist_ok=True)
            if target.exists(): assert sha(target)==e['sha256'], 'Will not overwrite different bytes: '+str(target)
            else: shutil.copyfile(ROOT/e['path'],target)
            assert sha(target)==e['sha256']
        (dest/'LOCAL_RESOURCE_INVENTORY.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(resources=len(entries),asset_ids=len(ids),fonts=len(fonts),
                         bytes=result['bundle_bytes'],all_frozen_tracked_bytes_match=True,
                         optional_qianqian_local_only=args.include_local_qianqian)))


if __name__=='__main__':
    main()
