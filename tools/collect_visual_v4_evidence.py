"""Publish only verified LVGL frames and previews; never reads private auth."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1]; REPORT=ROOT/'reports/visual-v4'; CAPTURE=ROOT/'reports/ui-v2/v4-final'; OUT=REPORT/'screenshots'; PREVIEW=REPORT/'previews'
OUT.mkdir(exist_ok=True); PREVIEW.mkdir(exist_ok=True)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
project=json.loads(Path('D:/build/visual-v4/project_description.json').read_text(encoding='utf-8'))
assert re.fullmatch(r'[0-9a-f]{7,40}',project['project_version']), 'Build a clean committed source first'
code=subprocess.check_output(['git','rev-parse',project['project_version']],text=True).strip()
subprocess.run(['git','diff','--quiet',code,'--','firmware/components/lw_ui','host','assets/visual-v4'],check=True)
rows=[]
for png in sorted(CAPTURE.glob('*.png')):
    bmp=png.with_suffix('.bmp')
    with Image.open(png) as a,Image.open(bmp) as b:
        assert a.size==b.size==(1024,600),png.name
        assert ImageChops.difference(a.convert('RGB'),b.convert('RGB')).getbbox() is None,png.name
    shutil.copyfile(png,OUT/png.name); rows.append({'file':'screenshots/'+png.name,'sha256':sha(png),'bytes':png.stat().st_size,'matches_framebuffer':True})
assert len(rows)==67,len(rows)
sequences={
 'boot':[f'v4-boot-{i:02}.png' for i in range(7)],
 'control-center':[f'v4-pull-{i:02}.png' for i in range(4)]+['v4-pull-open.png','v4-push-mid.png','v4-push-closed.png'],
 'letter':[f'v4-letter-{i:02}.png' for i in range(5)]+['v4-letter-closed.png'],
 'unlock':['v4-lock-00.png']+[f'v4-unlock-{i:02}.png' for i in range(3)],
 'greeting':[f'v4-greet-{i:02}.png' for i in range(8)]+['v4-greet-restored.png']}
preview_rows=[]
for name,names in sequences.items():
    frames=[Image.open(OUT/n).convert('RGB') for n in names]
    times=[200]*len(frames)
    if name=='greeting': times=[160]*8+[800]
    else: times[-1]=900
    gif=PREVIEW/(name+'.gif'); webp=PREVIEW/(name+'.webp')
    frames[0].save(gif,save_all=True,append_images=frames[1:],duration=times,loop=0)
    frames[0].save(webp,save_all=True,append_images=frames[1:],duration=times,lossless=True,loop=0)
    preview_rows.append({'file':'previews/'+gif.name,'lossless_webp':'previews/'+webp.name,'source_pngs':names,'timing':'sampled LVGL animation frames; review playback includes a final hold','source':'real LVGL framebuffer, not AI generation or Python layout composition','gif_palette_quantization':True,'sha256':sha(gif)})
baseline=ROOT/'reports/ui-v2/v4-baseline/01-desktop.png'
if baseline.exists(): shutil.copyfile(baseline,OUT/'baseline-windows.png')
manifest={'tested_code_commit':code,'public_images_authorized':True,'authorization':'2026-10-26 V4 task authorizes runtime screenshots, animation previews and assets','framebuffers':rows,'previews':preview_rows,'board_tested':False}
(REPORT/'SCREENSHOTS.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
runtime=json.loads((REPORT/'runtime/runtime.json').read_text(encoding='utf-8')); runtime.update(character_art_publication_authorized=True,publication_scope='V4 task approved rendered UI and specified art deliverables',tested_code_commit=code)
runtime['visual']=json.loads((CAPTURE/'visual.json').read_text(encoding='utf-8')); (REPORT/'runtime/runtime.json').write_text(json.dumps(runtime,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
log_specs={'v4-verified-host.log':'OS regression','v4-verified-fallback.log':'OS regression','v4-verified-missing-scene.log':'OS regression','v4-verified-core.log':'142 checks, 0 failure(s)','v4-verified-python.log':'Ran 18 tests','v4-verified-fonts.log':'font outputs are up to date','v4-verified-resources.log':'94 checks, 0 failures','v4-verified-legacy.log':'AUTO RUN: PASSED (0 failure(s))','v4-final-idf.log':'Project build complete.'}
logs=[]
for name,marker in log_specs.items():
    p=ROOT/'reports/logs'/name; data=p.read_text(encoding='utf-8',errors='replace')
    if marker=='OS regression':
        result=re.search(r'OS V2: (\d+) checks, 0 failures;',data); assert result and int(result.group(1))>=400,name; marker=result.group(0)
    else: assert marker in data,(name,marker)
    logs.append({'local_log':'reports/logs/'+name,'sha256':sha(p),'confirmed_result':marker})
protected=['firmware/main','firmware/components/lw_backend','firmware/components/lw_net','firmware/components/lw_system','firmware/components/lw_storage','firmware/components/lw_core','firmware/components/lw_duo','firmware/components/arcade_core','rpg','tests/host/test_rpg.c','firmware/partitions.csv']
subprocess.run(['git','diff','--quiet','d95b806','--',*protected],check=True)
assert project['project_version']==code[:7],project['project_version']
artifacts=[]
for p in [ROOT/'build/host/LittleWorld.exe',Path('D:/build/visual-v4/little_world.bin')]: artifacts.append({'local_path':p.as_posix(),'bytes':p.stat().st_size,'sha256':sha(p),'published':False})
verification={'tested_code_commit':code,'baseline_commit':'d95b806635659a1b3cf0282e306c9e603e7c734a','branch':'agent/visual-v4-meowa-motion','artifacts':artifacts,'logs':logs,'protected_paths_unchanged':protected,'framebuffers':67,'runtime_animation_previews':5,'asset_audit_checks':114,'meowa_actual_spent':169,'meowa_authorized_limit':1000,'idf':'5.5.5','lvgl':'9.5.0','target':project['target'],'silicon_build_min':project['min_rev'],'firmware_version':project['project_version'],'firmware_scope':'unchanged release firmware entry and backend; large scene files and new action packs are NOT embedded or mounted by this build','board_tested':False,'fixed_board_fps_claimed':False,'device_operations':0}
(REPORT/'VERIFICATION.json').write_text(json.dumps(verification,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'Verified {len(rows)} real PNG frames; published {len(sequences)} runtime animation previews; source {code}.')
