"""Non-network gates for actual Meowa component exports and runtime contracts."""
from pathlib import Path
import hashlib,json,struct
from PIL import Image,ImageSequence
R=Path(__file__).resolve().parents[1];A=R/'assets/visual-v4-3';checks=[]
def check(name,ok):checks.append({'check':name,'passed':bool(ok)})
m=json.loads((A/'components/MANIFEST.json').read_text('utf-8'));used=[]
for row in m['ui']:
    x,y,w,h=row['atlas_xywh'];check(row['id']+' inside bounded atlas',x>=0 and y>=0 and x+w<=512 and y+h<=544)
    for px,py,pw,ph in used:check(row['id']+' isolated atlas region',x+w<=px or px+pw<=x or y+h<=py or py+ph<=y)
    used.append((x,y,w,h))
im=Image.open(A/'components/atlas.png');check('binary alpha edges',sum(im.getchannel('A').histogram()[1:255])==0)
check('one atlas below individual-frame limit',im.size==(512,544) and 512*544*3<=1250000)
packs=[]
for p in sorted((A/'runtime').glob('*.lwp')):
    b=p.read_bytes();magic,w,h,n,_=struct.unpack_from('<8sHHHH',b);check(p.name+' format',magic==b'LWPX0001' and w<=1024 and h<=600 and w*h*3<=1250000)
    end=16+n*108;rows=[]
    for i in range(n):
        name,count,ax,ay,loop,_,start,*times=struct.unpack_from('<32sHhhBBI32H',b,16+i*108);name=name.rstrip(b'\0').decode();check(p.name+'/'+name+' payload',start==end and 0<count<=32 and all(20<=t<=2000 for t in times[:count]));end+=w*h*3*count;rows.append({'id':name,'frames':count,'anchor':[ax,ay]})
    check(p.name+' exact length',end==len(b));packs.append({'path':p.relative_to(R).as_posix(),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'canvas':[w,h],'clips':rows})
ledger=json.loads((R/'reports/visual-v4-3/component-pass/MEOWA_JOBS.json').read_text('utf-8'));check('actual component charges remain within human-approved remaining budget',sum(j.get('observed_balance_decrease',0) for j in ledger['jobs'])==ledger['actual_credits_spent']<=ledger['approved_limit'])
anim=Image.open(A/'components/dogbed/generation-original.webp');fs=[f.convert('RGBA') for f in ImageSequence.Iterator(anim)];check('whole puppy-bed idle stays fixed on one canvas',len(fs)==8 and all(f.size==(176,112) for f in fs));boxes=[f.getchannel('A').getbbox() for f in fs];check('bed loop anchor stable',len(set(boxes))==1)
result={'checks':len(checks),'failures':sum(not c['passed'] for c in checks),'results':checks,'packs':packs,'component_count':len(m['ui']),'bed_idle_boxes':boxes,'actual_component_credits':ledger['actual_credits_spent'],'pricing_discrepancies':ledger.get('pricing_discrepancies',[]),'user_art_acceptance':'pending; these gates do not certify taste','board_tested':False}
(R/'reports/visual-v4-3/component-pass/ASSET_AUDIT.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(f"Component audit: {len(checks)} checks, {result['failures']} failures.")
if result['failures']:raise SystemExit(1)
