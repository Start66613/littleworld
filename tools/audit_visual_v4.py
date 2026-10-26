"""Inspect delivered frames, scene conversions and raw packs without paid calls."""
from pathlib import Path
import hashlib,json,struct
from PIL import Image,ImageChops,ImageSequence
ROOT=Path(__file__).resolve().parents[1]; A=ROOT/'assets/visual-v4'; checks=0
def check(condition,message):
    global checks
    checks+=1
    if not condition: raise AssertionError(message)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def rgb565_bytes(image):
    import array,sys
    values=array.array('H',((r//8)*2048+(g//4)*32+b//8 for r,g,b in image.convert('RGB').get_flattened_data()))
    if sys.byteorder!='little': values.byteswap()
    return values.tobytes()
scene_rows=[]
for i,name in [(0,'sakura-morning'),(1,'moonlight-dream'),(2,'strawberry-sunset')]:
    png=A/'scenes'/f'{name}.png'; master=A/'scenes'/f'{name}-master.png'
    with Image.open(png) as im,Image.open(master) as large:
        check(im.size==(1024,600),'runtime scene dimensions'); check(large.size==(2048,1152),'master dimensions')
        check(ImageChops.difference(im.crop((0,12,1024,588)).convert('RGB'),large.resize((1024,576),Image.Resampling.NEAREST).convert('RGB')).getbbox() is None,'scene must not be stretched')
        for small in (False,True):
            p=A/'runtime'/f'scene-{i}{"-thumb" if small else ""}.lwr'; data=p.read_bytes(); magic,w,h,_=struct.unpack('<8sHHI',data[:16]); check(magic==b'LWSR0001' and len(data)==16+w*h*2,'scene header and exact payload'); src=im.resize((256,150),Image.Resampling.NEAREST) if small else im; check(data[16:]==rgb565_bytes(src),'raw scene round-trip')
        window=im.crop((30,30,235,210)).convert('RGB'); colors=tuple(round(v) for v in __import__('PIL.ImageStat',fromlist=['Stat']).Stat(window).mean)
    scene_rows.append({'name':name,'runtime_sha256':sha(png),'master_sha256':sha(master),'window_mean_rgb':colors,'raw_bytes':1228800,'thumbnail_bytes':76800})
animation_rows=[]
for character,actions in [('male',['idle','blink','wave']),('qianqian',['wave'])]:
    for name in actions:
        directory=A/'characters'/character; meta=json.loads((directory/(name+'.json')).read_text(encoding='utf-8')); frames=[Image.open(directory/p).convert('RGBA') for p in meta['frames']]
        check(len(frames)==len(meta['durations_ms']),'frame count matches timeline'); check(all(f.size==(256,256) for f in frames),'fixed canvas')
        check(all(f.getchannel('A').getextrema()[0]==0 for f in frames),'transparent margins'); check(all(f.getchannel('A').getbbox() for f in frames),'no empty frames')
        check(all(20<=n<=2000 for n in meta['durations_ms']),'valid frame timing'); check(len({f.tobytes() for f in frames})>1,'motion uses different pixels')
        if name=='wave': check(len(frames)==16 and sum(meta['durations_ms'])==1600,'wave fits unchanged core greet duration')
        with Image.open(directory/(name+'.webp')) as preview:
            check(preview.n_frames==len(frames),'WebP frame count')
            for actual,expected in zip(ImageSequence.Iterator(preview),frames): check(ImageChops.difference(actual.convert('RGBA'),expected).getbbox() is None,'lossless frame preview')
        floors=[f.getchannel('A').getbbox()[3] for f in frames]
        animation_rows.append({'character':character,'action':name,'frames':len(frames),'duration_ms':sum(meta['durations_ms']),'anchor':meta['anchor'],'foot_bottom_min':min(floors),'foot_bottom_max':max(floors),'rgba_raw_bytes_per_frame':262144,'rgb565_alpha_bytes_per_frame':196608,'preview':f'../../assets/visual-v4/characters/{character}/{name}.gif'})
for pack_name,character in [('male','male'),('qianqian-wave','qianqian')]:
    data=(A/'runtime'/(pack_name+'.lwp')).read_bytes(); magic,w,h,count,_=struct.unpack('<8sHHHH',data[:16]); check(magic==b'LWPX0001' and (w,h)==(256,256),'pack header')
    for i in range(count):
        record=struct.unpack('<32sHhhBBI32H',data[16+i*108:16+(i+1)*108]); name=record[0].split(b'\0')[0].decode(); n=record[1]; offset=record[6]; check(offset+n*196608<=len(data),'pack offsets bounded')
        for frame in {0,n//2,n-1}:
            with Image.open(A/'characters'/character/name/f'{frame:03}.png') as im:
                payload=data[offset+frame*196608:offset+(frame+1)*196608]; check(payload[:131072]==rgb565_bytes(im) and payload[131072:]==im.convert('RGBA').getchannel('A').tobytes(),'RGB565/alpha frame round-trip')
ledger=json.loads((ROOT/'reports/visual-v4/MEOWA_JOBS.json').read_text(encoding='utf-8')); spent=sum(j.get('observed_balance_decrease',0) for j in ledger['jobs']); check(spent==169 and spent<=ledger['authorized_limit'],'real spend within hard limit')
report={'checks':checks,'failures':0,'source_commit':__import__('subprocess').check_output(['git','rev-parse','HEAD'],text=True).strip(),'scenes':scene_rows,'animations':animation_rows,'meowa_actual_credits':spent,'scene_resident_upper_bound_bytes':1228800+3*76800,'windows_file_cache_budget_bytes':1572864,'lvgl_character_peak_from_regression_bytes':983040,'conservative_sum_of_separate_upper_bounds_bytes':1228800+3*76800+1572864+983040,'includes_display_buffers_fonts_or_system':False,'board_tested':False,'art_direction_user_acceptance':'pending'}
out=ROOT/'reports/visual-v4/ART_AUDIT.json'; out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8'); print(f'Visual asset audit: {checks} checks, 0 failures; actual spend {spent}/1000.')
