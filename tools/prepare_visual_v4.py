"""Reproducible local conversion of reviewed Meowa outputs (no API calls)."""
import array, hashlib, json, shutil, struct, sys
from pathlib import Path
from PIL import Image, ImageSequence
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets/visual-v4'; RUNTIME=ASSETS/'runtime'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def rgb565(im):
    buf=array.array('H',((r>>3)<<11|(g>>2)<<5|(b>>3) for r,g,b in im.convert('RGB').get_flattened_data()))
    if sys.byteorder!='little': buf.byteswap()
    return buf.tobytes()
def raw(im): return rgb565(im)+im.convert('RGBA').getchannel('A').tobytes()
def scene(number,name,source):
    dest=ASSETS/'scenes'; dest.mkdir(parents=True,exist_ok=True)
    master=dest/(name+'-master.png'); shutil.copyfile(source,master)
    with Image.open(source) as image:
        assert image.size==(2048,1152),image.size
        half=image.convert('RGB').resize((1024,576),Image.Resampling.NEAREST)
        final=Image.new('RGB',(1024,600)); final.paste(half,(0,12))
        final.paste(half.crop((0,0,1024,1)).resize((1024,12),Image.Resampling.NEAREST),(0,0))
        final.paste(half.crop((0,575,1024,576)).resize((1024,12),Image.Resampling.NEAREST),(0,588))
        final.save(dest/(name+'.png'))
        for thumb in (False,True):
            im=final.resize((256,150),Image.Resampling.NEAREST) if thumb else final
            p=RUNTIME/f'scene-{number}{"-thumb" if thumb else ""}.lwr'
            p.write_bytes(struct.pack('<8sHHI',b'LWSR0001',im.width,im.height,0)+rgb565(im))
    return {'theme':number,'name':name,'source':source.relative_to(ROOT).as_posix(),'source_sha256':digest(source),'master':master.relative_to(ROOT).as_posix(),'runtime_bytes':1228800,'thumbnail_bytes':76800,'conversion':'nearest 1/2, then edge-pad 12px top/bottom; no aspect stretch'}
def write_pack(path,clips,anchor_y):
    records=bytearray(); payload=bytearray(); offset=16+108*len(clips)
    for name,frames,times,loop in clips:
        assert len(frames)==len(times) and 0<len(frames)<=32
        records+=struct.pack('<32sHhhBBI32H',name.encode(),len(frames),128,anchor_y,int(loop),0,offset,*(times+[0]*(32-len(times))))
        for frame in frames: payload+=raw(frame); offset+=196608
    path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(struct.pack('<8sHHHH',b'LWPX0001',256,256,len(clips),0)+records+payload)
def animation(name,source,character,loop=True):
    dest=ASSETS/'characters'/character/name; dest.mkdir(parents=True,exist_ok=True)
    with Image.open(source) as im:
        frames=[]; times=[]
        for frame in ImageSequence.Iterator(im):
            rgba=frame.convert('RGBA'); assert rgba.size==(256,256),(source,rgba.size)
            frames.append(rgba.copy()); times.append(max(20,min(2000,frame.info.get('duration',125))))
    for i,frame in enumerate(frames): frame.save(dest/f'{i:03}.png')
    preview=dest.parent/(name+'.webp'); frames[0].save(preview,save_all=True,append_images=frames[1:],duration=times,loop=0,lossless=True,exact=True)
    frames[0].save(dest.parent/(name+'.gif'),save_all=True,append_images=frames[1:],duration=times,loop=0,disposal=2)
    original=dest.parent/(name+'-original.webp'); shutil.copyfile(source,original)
    metadata={'name':name,'character':character,'canvas':[256,256],'anchor':[128,225 if character=='male' else 226],'durations_ms':times,'loop':loop,'source':source.relative_to(ROOT).as_posix(),'source_sha256':digest(source),'frames':[f'{name}/{i:03}.png' for i in range(len(frames))],'bboxes':[f.getchannel('A').getbbox() for f in frames]}
    (dest.parent/(name+'.json')).write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    return name,frames,times,loop
def generated(name):
    files=list((ASSETS/'generated'/name).rglob('result_images_0_url.png')); return files[0] if files else None
def main():
    RUNTIME.mkdir(parents=True,exist_ok=True); refs=ROOT/'private/visual-v4/references'; scenes=[]
    for number,name,source in [(0,'sakura-morning',generated('sakura-morning-retry1')),(1,'moonlight-dream',generated('moonlight-dream')),(2,'strawberry-sunset',refs/'room_pixel_2048x1152_master.png')]:
        if source and source.exists(): scenes.append(scene(number,name,source))
    (ASSETS/'SCENES.json').write_text(json.dumps(scenes,indent=2)+'\n',encoding='utf-8')
    idle=next((ASSETS/'generated/male-idle').rglob('result_output_url.bin'),None)
    if idle:
        clips=[animation('idle',idle,'male')]
        with Image.open(idle) as im: idle_frames=[f.convert('RGBA') for f in ImageSequence.Iterator(im)]
        wave_source=next((ASSETS/'generated/male-wave').rglob('meowa_animation.webp'),None)
        if wave_source:
            with Image.open(wave_source) as im: im.seek(7); expression=im.convert('RGBA')
            blink=[f.copy() for f in idle_frames[4:8]]
            for i in (1,2):
                dy=blink[i].getchannel('A').getbbox()[1]-18
                for box in [(98,79,125,95),(132,79,158,95)]: blink[i].paste(expression.crop(box),(box[0],box[1]+dy))
            blink_file=ASSETS/'characters/male/blink-reviewed.webp'; blink[0].save(blink_file,save_all=True,append_images=blink[1:],duration=[125]*4,lossless=True,exact=True,loop=0)
            clips.append(animation('blink',blink_file,'male',False))
            (ASSETS/'characters/male/blink-transform.json').write_text(json.dumps({'source':'Meowa idle body frames 4..7 + closed eye pixels from accepted wave frame 7','eye_rectangles':[[98,79,125,95],[132,79,158,95]],'paid_calls':0,'body_translation':[0,0],'anchor_unchanged':True,'timing':'4x125ms, once per 5000ms idle period; eye patch follows existing head breathing only'},indent=2)+'\n',encoding='utf-8')
        wave=next((ASSETS/'characters/male').glob('wave-reviewed.webp'),None)
        if wave: clips.append(animation('wave',wave,'male',False))
        write_pack(RUNTIME/'male.lwp',clips,225)
    wave=next((ASSETS/'characters/qianqian').glob('wave-reviewed.webp'),None)
    if wave: write_pack(RUNTIME/'qianqian-wave.lwp',[animation('wave',wave,'qianqian',False)],226)
    print('Converted',len(scenes),'scenes; male pack:',(RUNTIME/'male.lwp').stat().st_size if (RUNTIME/'male.lwp').exists() else 'missing')
if __name__=='__main__': main()
