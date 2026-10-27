"""Reviewable production gates for the V4.2 native art exports; no API calls."""
import hashlib,json,struct
from pathlib import Path
from PIL import Image
R=Path(__file__).resolve().parents[1]; A=R/'assets/visual-v4-2'
checks=[]
def check(name,ok):
    checks.append({'check':name,'passed':bool(ok)})
def main():
    m=json.loads((A/'MANIFEST.json').read_text('utf-8'))
    for row in m['files']:
        p=R/row['path'];check(p.name+' content hash',hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'])
    packs=[]
    for p in sorted((A/'runtime').glob('*.lwp')):
        b=p.read_bytes();magic,w,h,n,_=struct.unpack_from('<8sHHHH',b)
        check(p.name+' format and bounded frame',magic==b'LWPX0001' and w*h*3<=1250000)
        clips=[]; offset=16+n*108
        for i in range(n):
            name,count,ax,ay,loop,_,start,*times=struct.unpack_from('<32sHhhBBI32H',b,16+i*108)
            name=name.rstrip(b'\0').decode(); check(p.name+'/'+name+' bounds',start==offset and 0<count<=32 and 0<=ax<w and 0<=ay<h and all(20<=t<=2000 for t in times[:count]))
            offset+=count*w*h*3;clips.append({'id':name,'frames':count,'duration_ms':sum(times[:count]),'anchor':[ax,ay]})
        check(p.name+' exact payload length',offset==len(b));packs.append({'path':p.relative_to(R).as_posix(),'canvas':[w,h],'bytes':len(b),'frame_bytes':w*h*3,'clips':clips})
    for clip in m['male_clips']:
        if not clip['id'].startswith('walk_'):continue
        boxes=clip['alpha_bboxes'];check(clip['id']+' formal visible scale',all(195<=b[3]-b[1]<=215 for b in boxes))
        check(clip['id']+' stable foot baseline',max(b[3] for b in boxes)-min(b[3] for b in boxes)<=5)
        check(clip['id']+' 800ms gait',sum(clip['durations_ms'])==800)
    for clip in m['clips']:
        if clip['id'].startswith('couple.'):
            boxes=clip['alpha_bboxes'];check(clip['id']+' safe transparent margins',all(b and b[0]>=8 and b[1]>=8 and b[2]<=312 and b[3]<=312 for b in boxes))
            check(clip['id']+' joint furniture anchor',clip['canvas']==[320,320] and clip['anchor']==[160,274])
    for name in ('rpg-workshop','daily-lounge'):
        with Image.open(A/'scenes'/(name+'.png')) as im:check(name+' screen canvas',im.size==(1024,600))
        with Image.open(A/'scenes'/(name+'-master.png')) as im:check(name+' untouched complete master',im.size==(2048,1152))
    ledger=json.loads((R/'reports/visual-v4-2/MEOWA_JOBS.json').read_text('utf-8'))
    spent=sum(j.get('observed_balance_decrease',0) for j in ledger['jobs']);check('all known generation charges within authorization',spent==ledger['actual_credits_spent'] and spent<=831 and sum(j['reserved_credits'] for j in ledger['jobs'])<=831)
    result={'checks':len(checks),'failures':sum(not c['passed'] for c in checks),'results':checks,'packs':packs,'meowa_spent':spent,'user_visual_acceptance':'pending; technical gates do not certify identity, perspective or aesthetic preference','hardware_tested':False,'memory':{'scene_cache_upper_bytes':2688000,'file_cache_upper_bytes':2761728,'lease_budget_bytes':2097152,'note':'independent counters; file cache and lease pixels may alias. Excludes LVGL objects, transforms, framebuffers, fonts, backend and stacks.'}}
    (R/'reports/visual-v4-2/ASSET_AUDIT.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f"V4.2 asset audit: {len(checks)} checks, {result['failures']} failures; {spent}/831 credits.")
    if result['failures']:raise SystemExit(1)
if __name__=='__main__':main()
