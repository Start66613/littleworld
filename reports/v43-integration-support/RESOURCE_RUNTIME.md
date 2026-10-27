# V4.3 冻结运行资源

冻结来源：`2900569e381f1e06ecf88d720a3f032a9bd97db6`。以下每个文件均与该提交的Git blob一致。完整SHA、帧时和锚点见RESOURCE_INVENTORY.json。所有数值是实际解析值。

## skin.lwp

- 源文件：`assets/visual-v4-3/runtime/skin.lwp`
- 本地交接目标：`runtime/skin.lwp`，由DSH放进自己验证的资源根目录。
- 格式：LWPX0001；512×544；文件835,708B，单帧835,584B。
- 资源ID：`v43skin.atlas`
- 加载：LWPX registry -> lw_assets -> lw_skin shared atlas lease。
- 缺失回退：Native LVGL controls; visual downgrade must be reported。

## daily.lwp

- 源文件：`assets/visual-v4-3/runtime/daily.lwp`
- 本地交接目标：`runtime/daily.lwp`，由DSH放进自己验证的资源根目录。
- 格式：LWPX0001；320×320；文件18,125,680B，单帧307,200B。
- 资源ID：`v42.couple.seated`, `v42.couple.wave`, `v42.couple.rest`, `v42.dog.idle`, `v42.dog.sleep`, `v42.dogbed.empty`, `v42.dogbed.sleep`, `v42.dogbed.look`
- 加载：LWPX registry -> lw_assets -> lw_sprite。
- 缺失回退：Old polish.lwp if explicitly supplied; otherwise labelled native pet view。

## male.lwp

- 源文件：`assets/visual-v4-2/runtime/male.lwp`
- 本地交接目标：`runtime/male.lwp`，由DSH放进自己验证的资源根目录。
- 格式：LWPX0001；256×256；文件17,892,100B，单帧196,608B。
- 资源ID：`male.idle`, `male.blink`, `male.wave`, `male.walk_down`, `male.walk_left`, `male.walk_right`, `male.walk_up`
- 加载：LWPX registry -> lw_actor_art_t[0] -> lw_sprite。
- 缺失回退：Embedded LWFA idle only; do not call it the full V4.3 motion set。

## qianqian-wave.lwp

- 源文件：`assets/visual-v4/runtime/qianqian-wave.lwp`
- 本地交接目标：`runtime/qianqian-wave.lwp`，由DSH放进自己验证的资源根目录。
- 格式：LWPX0001；256×256；文件3,145,852B，单帧196,608B。
- 资源ID：`qianqian.wave`
- 加载：Append to qianqian namespace; preserve base idle。
- 缺失回退：Original idle retained; missing wave must not replace the character。

## ui.lwp

- 源文件：`assets/visual-v4-2/runtime/ui.lwp`
- 本地交接目标：`runtime/ui.lwp`，由DSH放进自己验证的资源根目录。
- 格式：LWPX0001；544×352；文件4,021,372B，单帧574,464B。
- 资源ID：`v42ui.sheet`
- 加载：LWPX registry -> lw_assets -> RPG journal/dialogue/reward UI。
- 缺失回退：Native UI drawing。

## scene-0.lwr

- 源文件：`assets/visual-v4/runtime/scene-0.lwr`
- 本地交接目标：`runtime/scene-0.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_assets_configure(root) -> lw_visual_scene_image。
- 缺失回退：Native geometric room / theme thumbnail。

## scene-0-thumb.lwr

- 源文件：`assets/visual-v4/runtime/scene-0-thumb.lwr`
- 本地交接目标：`runtime/scene-0-thumb.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；256×150；文件76,816B，单帧76,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_assets_configure(root) -> lw_visual_scene_image。
- 缺失回退：Native geometric room / theme thumbnail。

## scene-1.lwr

- 源文件：`assets/visual-v4/runtime/scene-1.lwr`
- 本地交接目标：`runtime/scene-1.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_assets_configure(root) -> lw_visual_scene_image。
- 缺失回退：Native geometric room / theme thumbnail。

## scene-1-thumb.lwr

- 源文件：`assets/visual-v4/runtime/scene-1-thumb.lwr`
- 本地交接目标：`runtime/scene-1-thumb.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；256×150；文件76,816B，单帧76,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_assets_configure(root) -> lw_visual_scene_image。
- 缺失回退：Native geometric room / theme thumbnail。

## scene-2.lwr

- 源文件：`assets/visual-v4/runtime/scene-2.lwr`
- 本地交接目标：`runtime/scene-2.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_assets_configure(root) -> lw_visual_scene_image。
- 缺失回退：Native geometric room / theme thumbnail。

## scene-2-thumb.lwr

- 源文件：`assets/visual-v4/runtime/scene-2-thumb.lwr`
- 本地交接目标：`runtime/scene-2-thumb.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；256×150；文件76,816B，单帧76,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_assets_configure(root) -> lw_visual_scene_image。
- 缺失回退：Native geometric room / theme thumbnail。

## rpg-room.lwr

- 源文件：`assets/visual-v4-2/runtime/rpg-room.lwr`
- 本地交接目标：`runtime/rpg-room.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_aux_configure(root) -> lw_visual_rpg_image/lw_visual_daily_image。
- 缺失回退：Day/night falls back to daily-room; missing base returns native page。

## daily-room.lwr

- 源文件：`assets/visual-v4-2/runtime/daily-room.lwr`
- 本地交接目标：`runtime/daily-room.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_aux_configure(root) -> lw_visual_rpg_image/lw_visual_daily_image。
- 缺失回退：Day/night falls back to daily-room; missing base returns native page。

## daily-room-day.lwr

- 源文件：`assets/visual-v4-2/runtime/daily-room-day.lwr`
- 本地交接目标：`runtime/daily-room-day.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_aux_configure(root) -> lw_visual_rpg_image/lw_visual_daily_image。
- 缺失回退：Day/night falls back to daily-room; missing base returns native page。

## daily-room-night.lwr

- 源文件：`assets/visual-v4-2/runtime/daily-room-night.lwr`
- 本地交接目标：`runtime/daily-room-night.lwr`，由DSH放进自己验证的资源根目录。
- 格式：LWSR0001；1024×600；文件1,228,816B，单帧1,228,800B。
- 资源ID：由场景文件名寻址，不注册到lw_assets。
- 加载：lw_visual_aux_configure(root) -> lw_visual_rpg_image/lw_visual_daily_image。
- 缺失回退：Day/night falls back to daily-room; missing base returns native page。

## 中文字体

- `firmware/components/lw_ui/fonts/lw_font_18.c`：615,533B C源，18px，符号`lw_font_18`。
- `firmware/components/lw_ui/fonts/lw_font_22.c`：851,544B C源，22px，符号`lw_font_22`。
- `firmware/components/lw_ui/fonts/lw_font_28.c`：1,308,217B C源，28px，符号`lw_font_28`。
- `firmware/components/lw_ui/fonts/lw_font_44.c`：223,090B C源，44px，符号`lw_font_44`。
- `firmware/components/lw_ui/fonts/lw_font_88.c`：710,789B C源，88px，符号`lw_font_88`。

18/22/28为文楷屏幕版，44/88保留数字字体。字体是编译资源，不放TF卡；合并fonts与字体头文件并核对CMake，不能只复制皮肤图。C源码大小不代表运行RAM。

## 原版千千与注册顺序

本地完整交接另含`qianqian-base.lwp`：原有私有目录中的LWPX0001导出原字节复制，256×256、13动作、22,218,124B；仅本地，不加入公开Git。它不是新生成角色，也未修改私人数据。DSH也可沿用嵌入式LWFA的原版idle作为明确降级，但不能声称是完整Host动作集。

先把base注册为qianqian，再append wave；男方male；daily用v42；旧UI包用v42ui；skin用v43skin。全部加入同一张长寿命spec表后，仅发布一次，再启动OS。含完整千千时共31个ID、6个LWPX文件、5个namespace。不要在UI持有lease时替换或重置注册表。

调用`lw_visual_assets_configure(root)`配置桌面，再调用`lw_visual_aux_configure(root)`配置RPG/日常。两者可使用同一TF资源根目录；缺少后者会导致日常/RPG背景不存在。

## Host实际路径

`tools/run_visual_v4_3.ps1`设置LW_VISUAL_ASSETS、LW_POLISH_ASSETS、LW_MALE_PACK、LW_POLISH_PACK、LW_UI_PACK、LW_SKIN_PACK。`host/os_host.c`调用pixel_pack依次组合注册；不是自动扫描目录。`host/pixel_pack.c`按帧读取LWPX并预读下一帧。Host基包默认在private/pixel-duo/qianqian/qianqian.lwpx，生成脚本固定DesktopPet来源；仅复制公开assets不能替代这个依赖。
