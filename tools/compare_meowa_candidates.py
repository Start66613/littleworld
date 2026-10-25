"""Free local comparison after our independent submission/evaluation was frozen."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont
from analyze_meowa_challenge import measure
root=Path(__file__).resolve().parents[1]
paths=[('原版 Qianqian',Path(r'D:\Espressif\LittleWorld\sources\DesktopPet\assets\sprites\idle\idle_01.png')),
       ('DSH A',Path(r'D:\LittleWorld-Meowa\characters\v3a_pair\sprite_00.png')),
       ('DSH B',Path(r'D:\LittleWorld-Meowa\characters\v3b_pair\sprite_00.png')),
       ('Codex 单次',root/'assets/meowa-challenge/male-native.png')]
font=ImageFont.truetype(r'D:\LittleWorld-Main-DSH\tools\fonts\cache\NotoSansSC-Regular.otf',16)
preview=Image.new('RGBA',(1120,304),(255,250,244,255)); d=ImageDraw.Draw(preview); results=[]
for i,(name,path) in enumerate(paths):
    im=Image.open(path).convert('RGBA'); assert im.size==(256,256)
    m=measure(im); box=m['solid_bbox']; shift=(128-(box[0]+box[2])//2,226-box[3])
    aligned=Image.new('RGBA',(256,256)); aligned.paste(im,shift)
    preview.alpha_composite(aligned,(12+280*i,20)); d.text((24+280*i,272),f'{name} · {m["visible_size"][1]}px',font=font,fill='#51424a')
    results.append({'name':name,'source':str(path),'metrics':m,'translation':shift,'scale':1})
private=root/'private/meowa-challenge'
preview.convert('RGB').save(private/'comparison-dsh-codex-1x.png')
(root/'reports/meowa-challenge/comparison-metrics.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps([{'name':r['name'],'size':r['metrics']['visible_size'],'colors':r['metrics']['solid_colors'],'partial_alpha':r['metrics']['partial_alpha']} for r in results],ensure_ascii=False))
