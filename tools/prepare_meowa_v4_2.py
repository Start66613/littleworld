"""Technical export of reviewed Meowa art. Never cuts or composes room furniture.

Complete rooms only receive fixed 1/2 sampling and 12px edge padding. UI crops
come from the unmodified official component splitter. Original media is retained.
"""
import array, hashlib, json, os, re, shutil, struct, sys
from pathlib import Path
from PIL import Image, ImageSequence
from analyze_desktop_pet import parse_frames, original

ROOT=Path(__file__).resolve().parents[1]
A=ROOT/'assets/visual-v4-2'; G=A/'generated'; RUN=A/'runtime'
SOURCE=Path('D:/Espressif/LittleWorld/sources/DesktopPet')
rows=[]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def source(name,pattern):
    p=None if os.environ.get('LW_ART_EXPORT_PUBLIC_ONLY') else next((G/name).rglob(pattern),None)
    fallback={'rpg-workshop':A/'scenes/rpg-workshop-master.png','daily-lounge':A/'scenes/daily-lounge-master.png',
              'ui-storybook':A/'ui/generation-original.png','couple-idle':A/'characters/couple.seated/generation-original.webp',
              'couple-wave':A/'characters/couple.wave/generation-original.webp','couple-rest':A/'characters/couple.rest/generation-original.png',
              'couple-sofa-contact':A/'characters/couple-source/generation-contact.png','couple-sofa-alpha':A/'characters/couple-source/generation-extraction.png'}
    if name.startswith('male-walk-'): fallback[name]=A/'characters'/('male.walk_'+name.removeprefix('male-walk-'))/'generation-original.webp'
    return p or (fallback.get(name) if fallback.get(name) and fallback[name].exists() else None)
def copy(src,dst):
    if src.resolve()!=dst.resolve(): shutil.copyfile(src,dst)
def rgb565(im):
    a=array.array('H',((r>>3)<<11|(g>>2)<<5|(b>>3) for r,g,b in im.convert('RGB').get_flattened_data()))
    if sys.byteorder!='little': a.byteswap()
    return a.tobytes()
def pack(path,size,clips):
    offset=16+108*len(clips); records=bytearray(); data=bytearray()
    for name,frames,times,loop,anchor in clips:
        assert 0<len(frames)<=32 and len(frames)==len(times)
        records+=struct.pack('<32sHhhBBI32H',name.encode(),len(frames),*anchor,int(loop),0,offset,*(times+[0]*(32-len(times))))
        for im in frames:
            assert im.size==size
            blob=rgb565(im)+im.getchannel('A').tobytes(); data+=blob; offset+=len(blob)
    path.write_bytes(struct.pack('<8sHHHH',b'LWPX0001',*size,len(clips),0)+records+data)
def scene(name,file):
    src=source(name,'result_images_0_url.png'); assert src,name
    dst=A/'scenes';dst.mkdir(parents=True,exist_ok=True);copy(src,dst/(name+'-master.png'))
    with Image.open(src) as master:
        assert master.size==(2048,1152)
        half=master.convert('RGB').resize((1024,576),Image.Resampling.NEAREST)
    im=Image.new('RGB',(1024,600));im.paste(half,(0,12))
    im.paste(half.crop((0,0,1024,1)).resize((1024,12)),(0,0));im.paste(half.crop((0,575,1024,576)).resize((1024,12)),(0,588))
    im.save(dst/(name+'.png'));(RUN/file).write_bytes(struct.pack('<8sHHI',b'LWSR0001',1024,600,0)+rgb565(im))
    rows.append({'id':name,'source_sha256':sha(src),'canvas':[1024,600],'conversion':'complete image nearest 1/2, edge-pad 12px top/bottom; no furniture crop or composition','runtime':file})
def main():
    RUN.mkdir(parents=True,exist_ok=True)
    scene('rpg-workshop','rpg-room.lwr');scene('daily-lounge','daily-room.lwr')
    male=[]
    for action in ('idle','blink','wave'):
        d=ROOT/'assets/visual-v4/characters/male';meta=json.loads((d/(action+'.json')).read_text('utf-8'))
        male.append((action,[Image.open(d/f).convert('RGBA') for f in meta['frames']],meta['durations_ms'],action=='idle',(128,225)))
    for direction in ('down','left','right','up'):
        action='walk_'+direction; anim=source('male-walk-'+direction,'meowa_animation.webp')
        if not anim: continue
        with Image.open(anim) as im: frames=[f.convert('RGBA') for f in ImageSequence.Iterator(im)]
        assert all(f.size==(256,256) for f in frames)
        # Reviewed left clip starts with an unrelated front-facing pose. Keep the
        # original intact; exclude only that known defective opening frame.
        retained=list(range(1,len(frames))) if direction=='left' else list(range(len(frames)))
        frames=[frames[i] for i in retained]
        d=A/'characters'/('male.'+action);d.mkdir(parents=True,exist_ok=True);copy(anim,d/'generation-original.webp')
        bg=anim.with_name('meowa_animation_background.webp')
        if bg.exists(): copy(bg,d/'generation-background.webp')
        for old in d.glob('*.png'):
            if re.fullmatch(r'\d{3}\.png',old.name) and int(old.stem)>=len(frames): old.unlink()
        for i,f in enumerate(frames): f.save(d/f'{i:03}.png')
        times=[800*(i+1)//len(frames)-800*i//len(frames) for i in range(len(frames))]
        (d/'REVIEW.json').write_text(json.dumps({'original_frames':len(retained)+(1 if direction=='left' else 0),'retained_indices':retained,'reason':'exclude front-facing opening pose' if direction=='left' else 'full directional cycle retained','runtime_cycle_ms':800,'transform':'timing only; no per-frame recentering, no anatomy editing'},indent=2)+'\n')
        frames[0].save(d/'preview.webp',save_all=True,append_images=frames[1:],duration=times,loop=0,lossless=True,exact=True)
        male.append((action,frames,times,True,(128,226)))
    pack(RUN/'male.lwp',(256,256),male)
    clips=[]
    def add(name,frames,times,loop=True,anchor=(160,274)):
        mini=[im.convert('RGBA').copy() for im in frames]
        assert all(f.size==(320,320) for f in mini)
        dest=A/'characters'/name;dest.mkdir(parents=True,exist_ok=True)
        for i,f in enumerate(mini): f.save(dest/f'{i:03}.png')
        mini[0].save(dest/'preview.webp',save_all=True,append_images=mini[1:],duration=times,loop=0,lossless=True,exact=True)
        clips.append((name,mini,times,loop,anchor));rows.append({'id':name,'canvas':[320,320],'frames':len(mini),'durations_ms':times,'anchor':list(anchor),'alpha_bboxes':[im.getchannel('A').getbbox() for im in mini],'loop':loop})
    for name,job in [('seated','couple-idle'),('wave','couple-wave')]:
        anim=source(job,'meowa_animation.webp')
        if anim:
            with Image.open(anim) as im: frames=[f.convert('RGBA') for f in ImageSequence.Iterator(im)]
            d=A/'characters'/('couple.'+name);d.mkdir(parents=True,exist_ok=True);copy(anim,d/'generation-original.webp')
            bg=anim.with_name('meowa_animation_background.webp')
            if bg.exists(): copy(bg,d/'generation-background.webp')
        elif name=='seated': frames=[Image.open(A/'characters/couple-source/seated.png').convert('RGBA')]
        else: continue
        add('couple.'+name,frames,[100 if name=='wave' else 125]*len(frames),name!='wave')
    rest=source('couple-rest','edited.png')
    if rest:
        add('couple.rest',[Image.open(rest).convert('RGBA')],[1000])
        copy(rest,A/'characters/couple.rest/generation-original.png')
    for action,part in [('idle','idle/1786211809628-5htktvepw1s/1786211809628-5htktvepw1s_frames.tres'),('sleep','actions/sleep_idle/1786250482745-soeza6hm94b/1786250482745-soeza6hm94b_frames.tres')]:
        tres=SOURCE/'assets/dog/source'/part;text=tres.read_text('utf-8'); sheet=Image.open(tres.parent/re.search(r'path="([^"]+)"',text)[1]).convert('RGBA')
        regions=[tuple(map(int,m)) for m in re.findall(r'region = Rect2\((\d+), (\d+), (\d+), (\d+)\)',text)]
        weights=[float(v) for v in re.findall(r'"duration": ([\d.]+)',text)];speed=float(re.search(r'"speed": ([\d.]+)',text)[1]);frames=[]
        for x,y,w,h in regions:
            im=Image.new('RGBA',(320,320));im.alpha_composite(sheet.crop((x,y,x+w,y+h)),(128,203));frames.append(im)
        add('dog.'+action,frames,[round(1000*d/speed) for d in weights],True,(160,256))
    pack(RUN/'polish.lwp',(320,320),clips)
    # Native half-size components, centered only once per static component.
    # Frames: dialogue, journal, music box, polaroid, keepsake, lavender tab, pink tab.
    ui=[];dest=A/'ui';dest.mkdir(exist_ok=True)
    for index,n in enumerate((2,1,3,4,5,6,7)):
        p=G/'ui-components'/f'component_{n:03}.png'
        if p.exists() and not os.environ.get('LW_ART_EXPORT_PUBLIC_ONLY'):
            im=Image.open(p).convert('RGBA');factor=4 if n==3 else 2;im=im.resize(((im.width+factor-1)//factor,(im.height+factor-1)//factor),Image.Resampling.NEAREST)
            im.putalpha(im.getchannel('A').point(lambda a:255 if a>=128 else 0));frame=Image.new('RGBA',(544,352));frame.alpha_composite(im,((544-im.width)//2,(352-im.height)//2))
        else: frame=Image.open(dest/f'{index:02}.png').convert('RGBA')
        ui.append(frame)
        frame.save(dest/f'{len(ui)-1:02}.png')
    pack(RUN/'ui.lwp',(544,352),[('sheet',ui,[1000]*7,False,(0,0))])
    copy(source('ui-storybook','ui_output.png'),dest/'generation-original.png')
    contact=A/'characters/couple-source'
    copy(source('couple-sofa-contact','edited.png'),contact/'generation-contact.png')
    copy(source('couple-sofa-alpha','edited.png'),contact/'generation-extraction.png')
    manifest={'version':'visual-v4-2/meowa-1','rooms':rows[:2],'clips':rows[2:],'male_clips':[{'id':c[0],'canvas':[256,256],'frames':len(c[1]),'durations_ms':c[2],'anchor':list(c[4]),'alpha_bboxes':[im.getchannel('A').getbbox() for im in c[1]]} for c in male],'ui':{'canvas':[544,352],'components':7,'source':'official split-components, alpha threshold 8, min area 1500, padding 2','conversion':'dialogue/journal/frame/box/tabs nearest 1/2, music box 1/4, then transparent padding; alpha threshold128','source_sha256':sha(source('ui-storybook','ui_output.png'))},'files':[{'path':p.relative_to(ROOT).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(RUN.glob('*'))]}
    (A/'MANIFEST.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Exported complete Meowa rooms,',len(clips),'native clips and 7 UI components.')
if __name__=='__main__': main()
