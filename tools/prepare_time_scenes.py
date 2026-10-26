"""Export whole Meowa lighting variants; no scene cutouts or furniture edits."""
from pathlib import Path
from PIL import Image
import hashlib,json,struct
from prepare_meowa_v4_2 import rgb565,copy
R=Path(__file__).resolve().parents[1];out=R/'assets/visual-v4-3/scenes';out.mkdir(parents=True,exist_ok=True);rows=[]
for part in ('day','night'):
    p=next((R/'private/v43-generated'/('daily-'+part)).rglob('edited.png'),out/('daily-'+part+'-master.png'))
    im=Image.open(p).convert('RGB');assert im.size==(2048,1152)
    copy(p,out/('daily-'+part+'-master.png'));im=im.resize((1024,576),Image.Resampling.NEAREST)
    screen=Image.new('RGB',(1024,600));screen.paste(im,(0,12));screen.paste(im.crop((0,0,1024,1)).resize((1024,12)),(0,0));screen.paste(im.crop((0,575,1024,576)).resize((1024,12)),(0,588));screen.save(out/('daily-'+part+'.png'))
    dst=R/'assets/visual-v4-2/runtime'/('daily-room-'+part+'.lwr');dst.write_bytes(struct.pack('<8sHHI',b'LWSR0001',1024,600,0)+rgb565(screen))
    rows.append({'phase':part,'master_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'runtime':dst.relative_to(R).as_posix(),'sha256':hashlib.sha256(dst.read_bytes()).hexdigest(),'bytes':dst.stat().st_size,'transform':'complete nearest 1/2 plus fixed12px edge padding; no scene cuts or furniture layout'})
(R/'assets/visual-v4-3/MANIFEST.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
print('Two complete time scenes exported; existing LWR format and one auxiliary slot.')
