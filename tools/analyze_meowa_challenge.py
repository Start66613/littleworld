"""Native-pixel cutout and explicit composite previews; never redraw source Qianqian."""
import collections
import hashlib
import json
import shutil
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parents[1]
local=root/'private/meowa-challenge'
public=root/'assets/meowa-challenge'
public.mkdir(parents=True,exist_ok=True)
out=root/'reports/meowa-challenge'
manifest=json.loads((local/'split/final_outputs.json').read_text(encoding='utf-8'))
assert manifest['component_count']==1 and not manifest['warnings']
component=manifest['components'][0]
raw=next((local/'generated').rglob('edited.png'))
source=Image.open(raw).convert('RGBA')
assert source.size==(256,256)
shutil.copyfile(raw,public/'male-original.png')
crop=Image.open(local/'split'/component['file']).convert('RGBA')
male=Image.new('RGBA',(256,256)); male.paste(crop,tuple(component['bbox'][:2]))
male.save(public/'male-native.png')
bbox=male.getbbox()
aligned=Image.new('RGBA',(256,256)); shift=(128-(bbox[0]+bbox[2])//2,226-bbox[3])
aligned.paste(male,shift); aligned.save(public/'male-anchor-aligned.png')
qpath=Path(r'D:\Espressif\LittleWorld\sources\DesktopPet\assets\sprites\idle\idle_01.png')
girl=Image.open(qpath).convert('RGBA')
font=ImageFont.truetype(r'D:\LittleWorld-Main-DSH\tools\fonts\cache\NotoSansSC-Regular.otf',16)
pair=Image.new('RGBA',(576,304),(255,250,244,255))
pair.alpha_composite(girl,(16,20)); pair.alpha_composite(aligned,(304,20))
draw=ImageDraw.Draw(pair); draw.text((28,272),'原版 Qianqian · 198px',font=font,fill='#51424a'); draw.text((316,272),'Codex 单次候选 · 208px',font=font,fill='#51424a')
pair.convert('RGB').save(local/'pair-1x.png')
def measure(image):
    rgba=list(image.get_flattened_data()); solid=[p for p in rgba if p[3]>=128]
    colors=collections.Counter(p[:3] for p in solid)
    mask=image.getchannel('A').point(lambda v:255 if v>=128 else 0)
    box=mask.getbbox()
    return {'canvas':image.size,'solid_bbox':box,'visible_size':[box[2]-box[0],box[3]-box[1]],'solid_colors':len(colors),'partial_alpha':sum(0<p[3]<255 for p in rgba),'top_colors':[{'rgb':c,'pixels':n} for c,n in colors.most_common(8)]}
before=json.loads((local/'balance-before.json').read_text(encoding='utf-8-sig'))['total_credits']
after=json.loads((local/'balance-after.json').read_text(encoding='utf-8-sig'))['total_credits']
report={'job_id':'job_466dffc77e5647cfa02ea58434e8070c','command':'image-edit-run','model':'nano-banana','resolution':'1K','mode':'pixel','strict':True,'paid_submissions':1,'budget_cap':40,'quoted_conservative_estimate':12,'balance_before':before,'balance_after':after,'observed_balance_decrease':before-after,'job_response_has_itemized_credit_cost':False,'refund_reported':0,'cost_note':'Observed account net decrease; no per-job invoice field returned. Concurrent account activity is not attributed to this job.','qianqian':measure(girl),'male':measure(male),'anchor_aligned':{'translation_pixels':shift,'scale':1,'anchor':[128,226]},'visible_height_difference_px':measure(male)['visible_size'][1]-measure(girl)['visible_size'][1],'source_qianqian_sha256':hashlib.sha256(qpath.read_bytes()).hexdigest(),'public_files':[{ 'path':str(p.relative_to(root)).replace('\\','/'),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(public.glob('*.png'))],'qianqian_composites_publication_authorized':False,'real_photo_uploaded':False,'aesthetic_accepted':False}
background=local/'room-background.bmp'
if background.exists():
    room=Image.open(background).convert('RGBA')
    assert room.size==(1024,600)
    room.alpha_composite(girl,(804-128,446-226)); room.alpha_composite(aligned,(590-128,446-226))
    d=ImageDraw.Draw(room); d.text((32,18),'Little World · 素材合成预览（不是运行截图）',font=font,fill='#51424a'); d.text((40,530),'原图 1× / 原版千千 + Codex 单次男方候选 / 尚待审美验收',font=font,fill='#51424a')
    room.convert('RGB').save(local/'room-composite-1024x600.png')
(out/'metrics.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:report[k] for k in ['job_id','balance_before','balance_after','observed_balance_decrease','qianqian','male','anchor_aligned']},ensure_ascii=False,indent=2))
