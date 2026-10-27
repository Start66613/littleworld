"""Record the CURRENT DSH screenshots honestly; never relabel old frames as joint."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from PIL import Image,ImageChops

ROOT=Path(__file__).resolve().parents[1]
DSH=Path('D:/LittleWorld-V43')
OUT=ROOT/'reports/v43-integration-support'
OLD=Path('D:/LittleWorld-V41/reports/ui-v2/os-v2')
CAP=DSH/'reports/ui-v2/os-v2'
NAMES=['01-desktop.png','02-control-center.png','11-pet-idle.png','17-rpg-room.png']


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    head=subprocess.check_output(['git','-C',str(DSH),'rev-parse','HEAD'],text=True).strip()
    branch=subprocess.check_output(['git','-C',str(DSH),'branch','--show-current'],text=True).strip()
    ancestry=subprocess.run(['git','-C',str(DSH),'merge-base','--is-ancestor',
        '2900569e381f1e06ecf88d720a3f032a9bd97db6',head]).returncode==0
    rows=[]
    for name in NAMES:
        p=CAP/name; bmp=p.with_suffix('.bmp');old=OLD/name
        if not p.exists():continue
        with Image.open(p) as a,Image.open(bmp) as b:
            eq=a.size==b.size==(1024,600) and ImageChops.difference(a.convert('RGB'),b.convert('RGB')).getbbox() is None
        assert eq
        dest=OUT/'reviewed-dsh-old-ui'/name;dest.parent.mkdir(exist_ok=True);shutil.copyfile(p,dest)
        rows.append(dict(path=str(p),sha256=sha(p),matches_own_bmp=True,
                         identical_to_old_v41=old.exists() and sha(p)==sha(old),
                         reviewed_copy=dest.relative_to(ROOT).as_posix()))
    report=dict(dsh_head=head,dsh_branch=branch,frozen_visual_is_ancestor=ancestry,
                capture_provenance='Existing DSH PNGs verified against their BMPs. Their build source SHA was not recorded.',
                result='REJECT_AS_V43_JOINT_EVIDENCE', board_tested=False, screenshots=rows,
                findings=['Old full-width white Dock and large white anniversary card remain.',
                    'Pet view is the old geometric avatar; seated couple and joint dog-bed are absent.',
                    'RPG still shows TEST A+B and square floor; frozen V4.3 room/actors not present.',
                    'Current captures do not establish navigation return or day/night swapping.'],
                next='DSH merge V4.3 + reader compatibility, then capture the joint executable with source SHA and actual resource IDs.')
    (OUT/'JOINT_EVIDENCE.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    d=json.loads((OUT/'RESOURCE_INVENTORY.json').read_text('utf-8'))
    lines=['# V4.3 冻结运行资源','', '冻结来源：`'+d['frozen_commit']+'`。以下每个文件均与该提交的Git blob一致。完整SHA、帧时和锚点见RESOURCE_INVENTORY.json。所有数值是实际解析值。','']
    for e in d['resources']:
        lines += ['## '+e['target'],'', '- 源文件：`'+e['path']+'`',
            '- 本地交接目标：`runtime/'+e['target']+'`，由DSH放进自己验证的资源根目录。',
            f"- 格式：{e['format']}；{e['resolution'][0]}×{e['resolution'][1]}；文件{e['bytes']:,}B，单帧{e['frame_bytes']:,}B。",
            '- 资源ID：'+(', '.join('`'+i+'`' for i in e['ids']) if e['ids'] else '由场景文件名寻址，不注册到lw_assets。'),
            '- 加载：'+e['loader']+'。','- 缺失回退：'+e['fallback']+'。','']
    lines += ['## 中文字体','']
    for f in d['fonts']:
        lines += [f"- `{f['path']}`：{f['bytes']:,}B C源，{f['nominal_px']}px，符号`{f['id']}`。"]
    lines += ['', '18/22/28为文楷屏幕版，44/88保留数字字体。字体是编译资源，不放TF卡；合并fonts与字体头文件并核对CMake，不能只复制皮肤图。C源码大小不代表运行RAM。',
        '', '## 原版千千与注册顺序','',
        '本地完整交接另含`qianqian-base.lwp`：原有私有目录中的LWPX0001导出原字节复制，256×256、13动作、22,218,124B；仅本地，不加入公开Git。它不是新生成角色，也未修改私人数据。DSH也可沿用嵌入式LWFA的原版idle作为明确降级，但不能声称是完整Host动作集。',
        '', '先把base注册为qianqian，再append wave；男方male；daily用v42；旧UI包用v42ui；skin用v43skin。全部加入同一张长寿命spec表后，仅发布一次，再启动OS。含完整千千时共31个ID、6个LWPX文件、5个namespace。不要在UI持有lease时替换或重置注册表。',
        '', '调用`lw_visual_assets_configure(root)`配置桌面，再调用`lw_visual_aux_configure(root)`配置RPG/日常。两者可使用同一TF资源根目录；缺少后者会导致日常/RPG背景不存在。',
        '', '## Host实际路径','',
        '`tools/run_visual_v4_3.ps1`设置LW_VISUAL_ASSETS、LW_POLISH_ASSETS、LW_MALE_PACK、LW_POLISH_PACK、LW_UI_PACK、LW_SKIN_PACK。`host/os_host.c`调用pixel_pack依次组合注册；不是自动扫描目录。`host/pixel_pack.c`按帧读取LWPX并预读下一帧。Host基包默认在private/pixel-duo/qianqian/qianqian.lwpx，生成脚本固定DesktopPet来源；仅复制公开assets不能替代这个依赖。']
    (OUT/'RESOURCE_RUNTIME.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps(dict(dsh_head=head,captures_checked=len(rows),all_match_bmp=True,
                         all_match_old_v41=all(r['identical_to_old_v41'] for r in rows),
                         joint_acceptance='not passed; old UI captures')))


if __name__=='__main__':main()
