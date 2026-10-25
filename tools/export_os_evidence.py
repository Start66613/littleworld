"""Lossless conversion of real 1024x600 LVGL framebuffers, local only."""
import argparse
import json
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser(); p.add_argument('--root',type=Path,required=True); args=p.parse_args()
local=args.root/'reports/ui-v2/os-v2'
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
    (args.root/'reports/os-v2/runtime.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Actual LVGL screenshots:',len(list(local.glob('*.png'))),'(ignored, local only)')
