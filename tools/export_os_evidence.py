"""Lossless conversion of real 1024x600 LVGL framebuffers, local only."""
import argparse
import json
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser(); p.add_argument('--root',type=Path,required=True); p.add_argument('--input',default='reports/ui-v2/os-v2'); p.add_argument('--report-dir',default='reports/os-v2'); args=p.parse_args()
local=args.root/args.input
for file in sorted(local.glob('*.bmp')):
    with Image.open(file) as image:
        if image.size!=(1024,600): raise ValueError('Unexpected framebuffer dimensions')
        image.save(file.with_suffix('.png'))
report=local/'runtime.json'
if report.exists():
    result=json.loads(report.read_text(encoding='utf-8'))
    result['local_screenshots']=[f.name for f in sorted(local.glob('*.png'))]
    result['character_art_publication_authorized']=False
    storage=local/'storage.json'
    if storage.exists(): result['storage']=json.loads(storage.read_text(encoding='utf-8'))
    experience=local/'experience.json'
    if experience.exists(): result['experience']=json.loads(experience.read_text(encoding='utf-8'))
    target=args.root/args.report_dir; target.mkdir(parents=True,exist_ok=True)
    (target/'runtime.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Actual LVGL screenshots:',len(list(local.glob('*.png'))),'(ignored, local only)')
