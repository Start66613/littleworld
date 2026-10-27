"""Normalize Meowa's extracted interaction sprite; no room cutting or assembly.

The generative extraction changed the original coordinates. It is deliberately
NOT treated as an aligned room overlay. A single integer 1/3 sprite export is
compared to the reviewed joint contact keyframe before use or animation.
"""
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
A=ROOT/'assets/visual-v4-2'
p=next((A/'generated/couple-sofa-alpha').rglob('edited.png'),A/'characters/couple-source/generation-extraction.png')
im=Image.open(p).convert('RGBA'); box=im.getchannel('A').getbbox()
assert im.size==(2048,1152) and box==(710,240,1328,911), 'Review new output geometry first'
sprite=im.crop(box); sprite=sprite.resize((206,224),Image.Resampling.NEAREST)
out=Image.new('RGBA',(320,320)); out.alpha_composite(sprite,(57,50))
dest=A/'characters/couple-source'; dest.mkdir(parents=True,exist_ok=True)
out.save(dest/'seated.png')
data={'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'source_bbox':box,'source_canvas':[2048,1152],
      'source_was_coordinate_aligned':False,'conversion':'one uniform integer 1/3 sprite reduction (rounding last row), transparent padding; no anatomy edits or room crops',
      'sprite_canvas':[320,320],'alpha_bbox':[57,50,263,274],'anchor':[160,274],
      'room_contact_anchor':[516,360],'source_contact_master':'generated/couple-sofa-contact',
      'expected_joint_visible_size_from_contact_keyframe':[208,224],'export_visible_size':[206,224],
      'art_acceptance':'requires actual LVGL contact/identity review; not a pixel-identical overlay'}
(dest/'TRANSFORM.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Interaction source: native320, joint206x224, fixed anchor160,274; render contact review required.')
