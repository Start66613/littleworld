"""Publish verified, unretouched LVGL framebuffer evidence for V4.2."""
from pathlib import Path
import hashlib,html,json,re,shutil,subprocess
from PIL import Image,ImageChops
R=Path(__file__).resolve().parents[1]; D=R/'reports/visual-v4-2'; C=R/'reports/ui-v2/v42-final'
O=D/'screenshots';P=D/'previews';O.mkdir(exist_ok=True);P.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
code=subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()
source_paths=['firmware/components/lw_ui','host','assets/visual-v4-2','tools/prepare_meowa_v4_2.py']
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
previews=[]
for name,(names,times) in sequences.items():
    frames=[Image.open(O/n).convert('RGB') for n in names]
    for suffix in ('gif','webp'):
        args={'lossless':True,'exact':True} if suffix=='webp' else {}
        frames[0].save(P/(name+'.'+suffix),save_all=True,append_images=frames[1:],duration=times,loop=0,**args)
    previews.append({'name':name,'gif':'previews/'+name+'.gif','webp':'previews/'+name+'.webp','source_pngs':names,'duration_ms':times,'kind':'real sampled LVGL frames, review playback has final hold; no generated concept frames','sha256':sha(P/(name+'.webp'))})
baseline=[]
for name in ('17-rpg-room.png','11-pet-idle.png','14-music.png','13-album.png','18-rpg-journal.png','19-rpg-dialogue.png'):
    src=R/'reports/visual-v4/screenshots'/name
    if src.exists():
        b=D/'before';b.mkdir(exist_ok=True);shutil.copyfile(src,b/name);baseline.append({'before':'before/'+name,'after':'screenshots/'+name})
manifest={'tested_source_commit':code,'capture':'lv_refr_now -> Windows framebuffer BMP -> lossless PNG; byte-equivalent RGB checked','board_tested':False,'public_images_authorized':True,'framebuffers':rows,'previews':previews,'before_after':baseline}
(D/'SCREENSHOTS.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
specs={'v42-verified-host.log':'OS V2:','v42-verified-fallback.log':'OS V2:','v42-verified-missing-art.log':'OS V2:','v42-verified-resources.log':'94 checks, 0 failures','v42-verified-core.log':'142 checks, 0 failure(s)','v42-verified-python.log':'Ran 18 tests','v42-verified-fonts.log':'font outputs are up to date','v42-verified-legacy.log':'AUTO RUN: PASSED (0 failure(s))','v42-final-idf.log':'Project build complete.'}
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
verification={'tested_source_commit':code,'baseline':'94efbf590e5b7b2a51827edc46c03cb3c3a5237c','branch':'agent/visual-v4-2-polish','logs':logs,'artifacts':artifacts,'framebuffers':len(rows),'animation_previews':len(previews),'protected_paths_unchanged':protected,'idf':project['idf_ver'],'target':project['target'],'min_rev':project['min_rev'],'board_tested':False,'firmware_scope':'Shared UI compilation only; unchanged stable firmware entry. New LWPX/rooms are not embedded or mounted on hardware.','meowa_spent':436,'meowa_limit':831,'device_operations':0}
(D/'VERIFICATION.json').write_text(json.dumps(verification,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
parts=['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Little World V4.2 真实运行验收</title><style>body{margin:32px auto;max-width:1200px;background:#faf3ec;color:#51444d;font:18px system-ui}img{max-width:100%;image-rendering:pixelated;border-radius:12px}section{margin:32px 0} .pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}a{color:#955c79}h1{font-size:30px}small{font-size:15px}</style><h1>Little World V4.2</h1><p>全部画面来自实际 LVGL 帧缓冲。动画为真实采样帧，末帧加停留便于审阅。Windows 已测，实板未测；美术仍待用户验收。</p><p><a href="FINAL_REPORT.md">交付报告</a> · <a href="SCREENSHOTS.json">截图来源</a></p>']
for n in ('17-rpg-room.png','11-pet-idle.png','v42-daily-sleep.png','18-rpg-journal.png','19-rpg-dialogue.png','13-album.png','14-music.png','28-story-detail.png','21-games.png','04-settings.png'):
    if (O/n).exists():parts.append(f'<section><h2>{html.escape(n)}</h2><img src="screenshots/{n}"></section>')
for p in previews:parts.append(f'<section><h2>{p["name"]} · 真实运行</h2><img src="{p["webp"]}"><p><a href="{p["gif"]}">GIF</a></p></section>')
for p in baseline:parts.append(f'<section><h2>V4 → V4.2</h2><div class="pair"><img src="{p["before"]}"><img src="{p["after"]}"></div></section>')
parts.append(f'<small>验证源码 {code} · 无刷写、无设备操作</small></html>');(D/'index.html').write_text('\n'.join(parts),encoding='utf-8')
print(f'Published {len(rows)} verified LVGL screenshots, {len(previews)} motion previews; source {code}.')
