"""Officially segmented Meowa components -> shared LWPX atlas and joint bed clips.
Only technical UI packing/canvas conversion; never rearranges room furniture.
"""
import json,shutil,sys
from pathlib import Path
from PIL import Image,ImageSequence
from prepare_meowa_v4_2 import pack,copy
R=Path(__file__).resolve().parents[1];A=R/'assets/visual-v4-3/components';A.mkdir(parents=True,exist_ok=True)
S=R/'private/v43-components';rows=[]
def raw(which):return next((S/which).rglob('ui_output.png'),A/(which+'-original.png'))
def components(which):
    dest=A/which;dest.mkdir(exist_ok=True);source=S/(which+'-split')
    for p in source.glob('component_*.png'):copy(p,dest/p.name)
    if (source/'final_outputs.json').exists():copy(source/'final_outputs.json',dest/'SEGMENTATION.json')
    copy(raw(which),A/(which+'-original.png'));return dest
ui=components('ui');atlas=Image.new('RGBA',(512,544))
def put(name,path,rect,rotate=False):
    im=Image.open(path).convert('RGBA')
    if name=='dock':im=im.crop(im.getchannel('A').getbbox())
    if rotate:im=im.transpose(Image.Transpose.ROTATE_90)
    x,y,w,h=rect;scale=min(w/im.width,h/im.height);size=(max(1,round(im.width*scale)),max(1,round(im.height*scale)))
    im=im.resize(size,Image.Resampling.NEAREST);im.putalpha(im.getchannel('A').point(lambda a:255 if a>=128 else 0))
    atlas.alpha_composite(im,(x+(w-im.width)//2,y+(h-im.height)//2));rows.append({'id':name,'source':path.relative_to(R).as_posix(),'atlas_xywh':list(rect),'rotation':90 if rotate else 0,'visible_size':list(size)})
specs=[('dock',2,(0,0,512,44)),('time',9,(0,75,316,45)),('status-group',5,(316,75,196,28)),('action',7,(0,120,112,44)),('selected',6,(112,120,112,44)),('pressed',8,(224,120,112,44)),('collapsed',10,(336,120,64,44)),('close',11,(400,120,56,44)),('bookmark',4,(456,120,20,58)),('note',3,(0,178,148,136)),('book',1,(0,314,480,220))]
flat=next((S/'flat-dock').rglob('edited.png'),A/'flat-dock-original.png');copy(flat,A/'flat-dock-original.png')
for name,n,rect in specs:put(name,flat if name=='dock' else ui/f'component_{n:03}.png',rect,name=='collapsed')
control=S/'control-split'
if (control/'final_outputs.json').exists():
    c=components('control');meta=json.loads((c/'SEGMENTATION.json').read_text('utf-8'))
    # IDs are explicitly selected after visual review, not guessed from list order.
    selection_path=S/'control-selection.json'
    if not selection_path.exists():selection_path=c/'SELECTION.json'
    selection=json.loads(selection_path.read_text('utf-8'));copy(selection_path,c/'SELECTION.json')
    for name,rect in [('cc-panel',(152,178,128,128)),('cc-tile',(284,178,96,48)),('cc-selected',(380,178,96,48)),('cc-pressed',(284,230,96,48)),('slider',(492,178,16,128)),('knob',(380,230,32,40))]:put(name,c/f'component_{selection[name]:03}.png',rect)
atlas.save(A/'atlas.png');dest=R/'assets/visual-v4-3/runtime';dest.mkdir(exist_ok=True);pack(dest/'skin.lwp',(512,544),[('atlas',[atlas],[1000],False,(0,0))])
bed=components('dogbed');meta=json.loads((bed/'SEGMENTATION.json').read_text('utf-8'));entries=sorted(meta['components'],key=lambda c:c['bbox'][0]);assert len(entries)==3
for state,entry in zip(('empty','sleep','look'),entries):
    im=Image.open(bed/entry['file']).convert('RGBA');im=im.resize((im.width//2,im.height//2),Image.Resampling.NEAREST)
    im.putalpha(im.getchannel('A').point(lambda a:255 if a>=128 else 0));assert im.width<=176 and im.height<=112
    frame=Image.new('RGBA',(176,112));frame.alpha_composite(im,((176-im.width)//2,104-im.height));frame.save(bed/(state+'.png'))
# Keep old sources/clips intact; add the complete bed unit to the existing pack.
man=json.loads((R/'assets/visual-v4-2/MANIFEST.json').read_text('utf-8'));clips=[]
for clip in man['clips']:
    d=R/'assets/visual-v4-2/characters'/clip['id'];frames=[Image.open(d/f'{i:03}.png').convert('RGBA') for i in range(clip['frames'])]
    clips.append((clip['id'],frames,clip['durations_ms'],clip['loop'],tuple(clip['anchor'])))
for state in ('empty','sleep','look'):
    anim=None
    if state=='look':
        outputs=next((S/'dogbed-idle').rglob('final_outputs.json'),None)
        if outputs:
            declared=json.loads(outputs.read_text('utf-8'));anim=next((Path(o['path']) for o in declared['outputs'] if o.get('mime_type')=='image/webp'),None)
            if anim:copy(anim,bed/'generation-original.webp')
        elif (bed/'generation-original.webp').exists():anim=bed/'generation-original.webp'
    ims=[f.convert('RGBA') for f in ImageSequence.Iterator(Image.open(anim))] if anim else [Image.open(bed/(state+'.png')).convert('RGBA')]
    frames=[]
    for im in ims:
        assert im.size==(176,112)
        frame=Image.new('RGBA',(320,320));frame.alpha_composite(im,(72,170));frames.append(frame)
    clips.append(('dogbed.'+state,frames,[125]*len(frames),True,(160,274)))
pack(dest/'daily.lwp',(320,320),clips)
(A/'MANIFEST.json').write_text(json.dumps({'atlas_canvas':[512,544],'atlas_bytes':512*544*3,'ui':rows,'bed_states':['empty','sleep','look'],'note':'Meowa components, official alpha segmentation; nearest technical fitting, one shared atlas lease, no baked dynamic labels'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Packed',len(rows),'UI component skins and three complete dog-bed states.')
