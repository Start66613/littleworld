"""Publish verified, unretouched LVGL framebuffer evidence for V4.3."""
from pathlib import Path
import hashlib,html,json,re,shutil,subprocess
from PIL import Image,ImageChops
R=Path(__file__).resolve().parents[1]; D=R/'reports/visual-v4-3'; C=R/'reports/ui-v2/v43-final'
O=D/'screenshots';P=D/'previews';O.mkdir(exist_ok=True);P.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
code=subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()
source_paths=['firmware/components/lw_ui','host','assets/visual-v4-2','assets/visual-v4-3','tools/prepare_meowa_v4_2.py']
subprocess.run(['git','diff','--quiet',code,'--',*source_paths],cwd=R,check=True)
project=json.loads(Path('D:/build/visual-v4-2/project_description.json').read_text('utf-8'))
assert re.fullmatch('[0-9a-f]{7,40}',project['project_version']),project['project_version']
idf_code=subprocess.check_output(['git','rev-parse',project['project_version']],cwd=R,text=True).strip();assert idf_code==code
rows=[]
for p in sorted(C.glob('*.png')):
    with Image.open(p) as a,Image.open(p.with_suffix('.bmp')) as b:
        assert a.size==b.size==(1024,600)
        assert ImageChops.difference(a.convert('RGB'),b.convert('RGB')).getbbox() is None,p.name
    shutil.copyfile(p,O/p.name);rows.append({'file':'screenshots/'+p.name,'sha256':sha(p),'matches_framebuffer':True})
sequences={
 'boot':([f'v4-boot-{i:02}.png' for i in range(11)],[200]*10+[900]),
 'control-center':([f'v4-pull-{i:02}.png' for i in range(4)]+['v4-pull-open.png','v4-push-mid.png','v4-push-closed.png'],[40]*4+[500,80,900]),
 'envelope':([f'v4-letter-{i:02}.png' for i in range(5)]+['v4-letter-closed.png'],[160]*5+[900]),
 'unlock':(['v4-lock-00.png']+[f'v4-unlock-{i:02}.png' for i in range(3)],[700,160,60,900]),
 'daily-greeting':([f'v42-daily-wave-{i:02}.png' for i in range(10)]+['v42-daily-restored.png'],[120]*10+[800]),
 'daily-idle':([f'v42-daily-idle-{i:02}.png' for i in range(8)],[160]*7+[800]),
 'rpg-reward':([f'v42-rpg-reward-{i:02}.png' for i in range(8)],[100]*7+[800]),
 'rpg-walk':([f'v42-rpg-walk-{i:02}.png' for i in range(12)],[80]*11+[800])}
sequences['dock-reveal']=([f'v43-dock-open-{i:02}.png' for i in range(8)],[60]*7+[900])
sequences['home-day-cycle']=([f'v43-home-{n}.png' for n in ('dawn','day','dusk','night','deep-night')],[1400]*5)
sequences['daily-day-cycle']=([f'v43-daily-{n}.png' for n in ('dawn','day','dusk','night','deep-night')],[1400]*5)
sequences['storybook']=([f'v43-book-open-{i:02}.png' for i in range(8)]+['28-story-detail.png']+[f'v43-book-close-{i:02}.png' for i in range(5)],[80]*8+[900]+[80]*4+[900])
sequences['puppy-in-bed']=([f'component-dog-idle-{i:02}.png' for i in range(8)],[125]*7+[600])
previews=[]
for name,(names,times) in sequences.items():
    frames=[Image.open(O/n).convert('RGB') for n in names]
    for suffix in ('gif','webp'):
        args={'lossless':True,'exact':True} if suffix=='webp' else {}
        frames[0].save(P/(name+'.'+suffix),save_all=True,append_images=frames[1:],duration=times,loop=0,**args)
    kind='five actual settings time-preview captures, held 1.4s each; not an accelerated 24-hour recording' if name.endswith('day-cycle') else 'real sampled LVGL animation; review playback adds final hold, no generated concept frames'
    previews.append({'name':name,'gif':'previews/'+name+'.gif','webp':'previews/'+name+'.webp','source_pngs':names,'duration_ms':times,'kind':kind,'sha256':sha(P/(name+'.webp'))})
baseline=[]
for name in ('17-rpg-room.png','11-pet-idle.png','14-music.png','13-album.png','18-rpg-journal.png','19-rpg-dialogue.png'):
    src=R/'reports/visual-v4/screenshots'/name
    if src.exists():
        b=D/'before';b.mkdir(exist_ok=True);shutil.copyfile(src,b/name);baseline.append({'before':'before/'+name,'after':'screenshots/'+name})
manifest={'tested_source_commit':code,'capture':'lv_refr_now -> Windows framebuffer BMP -> lossless PNG; byte-equivalent RGB checked','board_tested':False,'public_images_authorized':True,'framebuffers':rows,'previews':previews,'before_after':baseline}
(D/'SCREENSHOTS.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
specs={'v43-verified-host.log':'OS V2:','v43-verified-fallback.log':'OS V2:','v43-verified-missing-art.log':'OS V2:','v43-verified-resources.log':'94 checks, 0 failures','v43-verified-core.log':'142 checks, 0 failure(s)','v43-verified-python.log':'Ran 18 tests','v43-verified-fonts.log':'font outputs are up to date','v43-verified-legacy.log':'AUTO RUN: PASSED (0 failure(s))','v43-final-idf.log':'Project build complete.'}
logs=[]
for name,marker in specs.items():
    p=R/'reports/logs'/name;s=p.read_text('utf-8',errors='replace')
    if marker=='OS V2:':
        m=re.search(r'OS V2: (\d+) checks, 0 failures;',s);assert m and int(m[1])>=480,name;result=m[0]
    else:assert marker in s,(name,marker);result=marker
    logs.append({'local_log':p.relative_to(R).as_posix(),'sha256':sha(p),'confirmed':result})
protected=['firmware/main','firmware/components/lw_backend','firmware/components/lw_net','firmware/components/lw_system','firmware/components/lw_storage','firmware/components/lw_core','firmware/components/lw_duo','firmware/components/arcade_core','rpg','tests/host/test_rpg.c','firmware/partitions.csv']
subprocess.run(['git','diff','--quiet','94efbf5','--',*protected],cwd=R,check=True)
runtime=json.loads((C/'runtime.json').read_text('utf-8'));runtime.update(tested_source_commit=code,character_art_publication_authorized=True)
for n in ('experience','storage','visual'):runtime[n]=json.loads((C/(n+'.json')).read_text('utf-8'))
(D/'runtime/runtime.json').write_text(json.dumps(runtime,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
artifacts=[{'local_path':str(p),'bytes':p.stat().st_size,'sha256':sha(p),'published':False} for p in (R/'build/host/LittleWorld.exe',Path('D:/build/visual-v4-2/little_world.bin'))]
verification={'tested_source_commit':code,'baseline':'2765d5733d0b98da4b1b5e56da9893fc66da7800','branch':'agent/visual-v4-2-polish','logs':logs,'artifacts':artifacts,'framebuffers':len(rows),'animation_previews':len(previews),'protected_paths_unchanged':protected,'idf':'5.5.5','target':project['target'],'min_rev':project['min_rev'],'board_tested':False,'firmware_scope':'Shared UI compilation only; unchanged stable firmware entry. New LWPX/rooms are not embedded or mounted on hardware.','meowa_time_scenes':44,'meowa_component_pass':115,'meowa_cumulative_original_budget':764,'original_budget_remaining':236,'device_operations':0}
(D/'VERIFICATION.json').write_text(json.dumps(verification,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
parts=['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Little World V4.3 真实运行验收</title><style>body{margin:32px auto;max-width:1200px;background:#faf3ec;color:#51444d;font:18px system-ui}img{max-width:100%;image-rendering:pixelated;border-radius:12px}section{margin:32px 0} .pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}a{color:#955c79}h1{font-size:30px}small{font-size:15px}</style><h1>Little World V4.3</h1><p>全部画面来自实际 LVGL 帧缓冲。动画为真实采样帧，末帧加停留便于审阅。Windows 已测，实板未测；美术仍待用户验收。</p><p><a href="FINAL_REPORT.md">交付报告</a> · <a href="SCREENSHOTS.json">截图来源</a></p>']
for n in ('v43-dock-hidden.png','v43-home-day.png','v43-home-night.png','v43-daily-day.png','v43-daily-night.png','v43-time-settings.png','17-rpg-room.png','11-pet-idle.png','v42-daily-sleep.png','18-rpg-journal.png','19-rpg-dialogue.png','13-album.png','14-music.png','28-story-detail.png','21-games.png','04-settings.png'):
    if (O/n).exists():parts.append(f'<section><h2>{html.escape(n)}</h2><img src="screenshots/{n}"></section>')
for p in previews:parts.append(f'<section><h2>{p["name"]} · 真实运行</h2><img src="{p["webp"]}"><p><a href="{p["gif"]}">GIF</a></p></section>')
for p in baseline:parts.append(f'<section><h2>V4 → V4.3</h2><div class="pair"><img src="{p["before"]}"><img src="{p["after"]}"></div></section>')
for name,after in [('home','v43-home-night.png'),('daily','v43-daily-day.png'),('rpg','17-rpg-room.png')]:
    parts.append(f'<section><h2>V4.2 → V4.3 · {name}</h2><div class="pair"><img src="before/{name}.png"><img src="screenshots/{after}"></div></section>')
for name,after in [('home','v43-home-night.png'),('daily','v43-daily-day.png'),('control','02-control-center.png'),('book','28-story-detail.png')]:
    parts.append(f'<section><h2>本轮组件精修前 → 后 · {name}</h2><div class="pair"><img src="before/components-{name}.png"><img src="screenshots/{after}"></div></section>')
parts.append(f'<small>验证源码 {code} · 无刷写、无设备操作</small></html>');(D/'index.html').write_text('\n'.join(parts),encoding='utf-8')
print(f'Published {len(rows)} verified LVGL screenshots, {len(previews)} motion previews; source {code}.')
