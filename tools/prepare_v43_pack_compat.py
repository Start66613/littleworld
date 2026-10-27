"""Make a narrowly scoped patch against DSH's frozen reader, without editing it."""
import difflib
import hashlib
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DSH=Path('D:/LittleWorld-V43')
BASE='05bfc36b335a5a4636f38d641a0fefde4dbd6599'
OUT=ROOT/'reports/v43-integration-support'
SNAP=ROOT/'build/v43-pack-compat'


def replace(text, before, after, count=1):
    assert text.count(before)==count, before
    return text.replace(before,after)


def main():
    paths=['firmware/components/lw_ui/include/lw_pack.h','firmware/components/lw_ui/src/lw_pack.c']
    diffs=[]; rows=[]
    for rel in paths:
        original=subprocess.check_output(['git','-C',str(DSH),'show',BASE+':'+rel]).decode('utf-8')
        updated=original
        if rel.endswith('.h'):
            updated=replace(updated,'#define LW_PACK_MAX_CLIPS 6u','#define LW_PACK_MAX_CLIPS 32u')
            updated=updated.replace('a 256x256 RGB565+A8 frame','a per-pack RGB565+A8 frame')
            updated=updated.replace('Up to LW_PACK_MAX_FILES (4) files','Up to LW_PACK_MAX_FILES (8) files')
            updated=updated.replace('/* Registration budget handed to lw_assets: two live frames per actor. */',
                '/* Legacy convenience budget only. The combined V4.3 registry must\n'
                ' * explicitly publish with 2 MiB; the atlas alone is 835584 bytes. */')
            updated=updated.replace(' * layout. What it shares',
                ' * layout (256x256 actors, 320x320 daily, 544x352 UI, 512x544 atlas).\n * What it shares')
        else:
            updated=replace(updated,'#define LW_PACK_MAX_ACTORS 2u','#define LW_PACK_MAX_ACTORS 5u /* two actors + v42 + v42ui + v43skin */')
            updated=replace(updated,'#define LW_PACK_MAX_SPECS (LW_PACK_MAX_ACTORS * LW_PACK_MAX_CLIPS)',
                '#define LW_PACK_MAX_SPECS 64u /* same bound as lw_assets */')
            updated=replace(updated,'#define LW_PACK_MAX_FILES 4u','#define LW_PACK_MAX_FILES 8u')
            updated=replace(updated,'    s_caches[0] = NULL;\n    s_caches[1] = NULL;',
                '    memset(s_caches, 0, sizeof(s_caches));')
            updated=replace(updated,'    if (u16(header + 8) != LW_PACK_SIDE || u16(header + 10) != LW_PACK_SIDE) {',
                '    const unsigned width = u16(header + 8), height = u16(header + 10);\n'
                '    /* Frozen V4.3 geometries only: do not widen to unbounded art. */\n'
                '    if (!((width == 256u && height == 256u) ||\n'
                '          (width == 320u && height == 320u) ||\n'
                '          (width == 544u && height == 352u) ||\n'
                '          (width == 512u && height == 544u))) {')
            updated=replace(updated,'    unsigned clips = u16(header + 12);',
                '    const uint32_t frame_bytes = width * height * 3u;\n    unsigned clips = u16(header + 12);')
            updated=replace(updated,'anchor_x > (int)LW_PACK_SIDE || anchor_y > (int)LW_PACK_SIDE',
                'anchor_x > (int)width || anchor_y > (int)height')
            updated=replace(updated,'(uint64_t)frames * LW_PACK_FRAME_BYTES > (uint64_t)size',
                '(uint64_t)frames * frame_bytes > (uint64_t)size')
            updated=replace(updated,'s_sources[spec].frame_bytes = LW_PACK_FRAME_BYTES;',
                's_sources[spec].frame_bytes = frame_bytes;')
            updated=replace(updated,'s_file_paths[file_slot], offset, LW_PACK_FRAME_BYTES, (uint16_t)frames',
                's_file_paths[file_slot], offset, frame_bytes, (uint16_t)frames')
            updated=replace(updated,'(lw_asset_spec_t){s_sources[spec].id, (uint16_t)LW_PACK_SIDE,\n                                          (uint16_t)LW_PACK_SIDE,',
                '(lw_asset_spec_t){s_sources[spec].id, (uint16_t)width,\n                                          (uint16_t)height,')
            updated=updated.replace('LWPX is always 256x256x3, but the embedded LWFA pack',
                'LWPX takes geometry from its validated header; the embedded LWFA pack')
            updated=updated.replace('a 256x256 RGB565+A8 frame','a per-pack RGB565+A8 frame')
        target=SNAP/'patched'/Path(rel).name
        target.parent.mkdir(parents=True,exist_ok=True); target.write_text(updated,encoding='utf-8',newline='\n')
        base=SNAP/'original'/Path(rel).name
        base.parent.mkdir(parents=True,exist_ok=True);base.write_text(original,encoding='utf-8',newline='\n')
        diffs.extend(difflib.unified_diff(original.splitlines(True),updated.splitlines(True),'a/'+rel,'b/'+rel))
        rows.append(dict(path=rel,before_sha256=hashlib.sha256(original.encode()).hexdigest(),after_sha256=hashlib.sha256(updated.encode()).hexdigest()))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'lw-pack-v43-compat.patch').write_text(''.join(diffs),encoding='utf-8',newline='\n')
    (OUT/'PATCH_BASE.json').write_text(json.dumps(dict(dsh_base=BASE,files=rows,
        scope='Pure UI resource parser only. DSH decides when to apply; no boot/hardware files changed.',
        accepted_geometries=[[256,256],[320,320],[544,352],[512,544]],
        namespaces=5,max_files=8,max_specs=64,max_clips_per_namespace=32,
        boot_requirement='Register all namespaces once, with 2MiB lease ceiling. Atlas should use NULL file cache if existing cache is smaller than835584.'),indent=2)+'\n',encoding='utf-8')
    sprite_diffs=[]
    for rel in ('firmware/components/lw_ui/src/lw_sprite.c','firmware/components/lw_ui/include/lw_sprite.h'):
        original=subprocess.check_output(['git','-C',str(DSH),'show',BASE+':'+rel]).decode('utf-8')
        if rel.endswith('.c'):
            updated=replace(original,'scale>256) return NULL; /* Export a larger asset rather than upscale. */',
                'scale>320) return NULL; /* Frozen V4.3 uses 288 standing / 282 seated. */')
            updated=replace(updated,'if (!s || s->scale!=256) return false;', 'if (!s) return false;')
        else:
            updated=replace(original,'/* Pixel mode is native 1x with antialiasing disabled; not fractional zoom. */',
                '/* Nearest-neighbor pixel mode; frozen V4.3 also uses bounded 282/288 scale. */')
        (SNAP/'patched'/Path(rel).name).write_text(updated,encoding='utf-8',newline='\n')
        (SNAP/'original'/Path(rel).name).write_text(original,encoding='utf-8',newline='\n')
        sprite_diffs.extend(difflib.unified_diff(original.splitlines(True),updated.splitlines(True),'a/'+rel,'b/'+rel))
    (OUT/'lw-sprite-v43-scale.patch').write_text(''.join(sprite_diffs),encoding='utf-8',newline='\n')
    # Keep transitive #pragma-once headers in the same snapshot directory;
    # otherwise the compiler can include both original and patched sprite.h.
    for name in ('lw_duo_view.h','lw_scene.h'):
        text=subprocess.check_output(['git','-C',str(DSH),'show',BASE+':firmware/components/lw_ui/include/'+name]).decode('utf-8')
        for variant in ('original','patched'):(SNAP/variant/name).write_text(text,encoding='utf-8',newline='\n')
    print('Prepared patch against '+BASE+'; no DSH files modified.')


if __name__=='__main__':main()
