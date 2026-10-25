# qianqian 与男方角色 · 像素美术契约 1.0.1

日期：2026-10-25（Asia/Shanghai）。角色方向已由用户确定；男方最终形象仍须用户确认。Codex 独占维护，DSH 只读。替代旧绘本契约，不删除 art/batch-1 或旧素材。

## 实际来源与身份
已获取并逐帧读取 https://github.com/Start66613/DesktopPet ，固定提交 b00a7005f4889644737092dbc7379e2a9b011315。本地只读来源 D:\Espressif\LittleWorld\sources\DesktopPet。重点实证：assets/runtime/pet_sprite_frames.tres、scenes/pet.tscn、scripts/pet.gd、assets/sprites/idle/、assets/movement/、assets/activities/、docs/ASSET_CATALOG.md。

女方就是黑发、粉白裙、粉色蝴蝶结、花饰/侧辫的像素少女 qianqian。小雪球属于 assets/dog/，是独立辅助狗；dog_interaction 是 qianqian 与狗的合成动作，绝不能拿来当男方或双人动作。龙骑乘同样不能当普通步行帧。

非 AI 重绘参考图集：D:\LittleWorld-Codex\private\pixel-duo\qianqian\qianqian-reference.png。统计：reports/pixel-duo/qianqian-analysis.json。图片、运行像素包与真实角色截图本地忽略；未经用户确认并明确授权公开不提交公共仓库。

## 实测画风与比例
- idle 8 帧，每帧 256×256。首帧 alpha 包围盒 [78,28,176,226)，实际人物高 198 px、宽 98 px；8 帧高 192–198 px、底边一致 y=226。不是 32×32 小人，也不是放大 8 倍的粗像素。
- 原生约 1 px 细轮廓，局部 2 px 深轮廓；黑/深棕轮廓不等于每处纯黑描边。头身约 2.3–2.6 头高，为视觉量测范围；精确脸部结构以原图为准。
- 黑发以暖深灰 #393834、#23221F 及 #504F4C 分层，肤色偏暖，粉白裙/蝴蝶结为低饱和粉。idle 首帧 alpha≥128 的 RGB 实测 716 种，不强行降成 16 色，不以色数少作为像素风合格证明。
- 眼睛占面部明显面积，有深棕瞳孔/亮点；脸部能读出视线和困倦，不能把男方做成无表情的两颗黑点。原有反锯齿像素属于源画，不再做平滑重采样。
- 1× 显示：角色画布 256 px，实际直立人物约 198 px，适配 1024×600。默认原生显示；仅允许 1×/2× 等整数最近邻预处理版本。禁止 0.8×、1.25× 或双线性缩放，禁止为了塞进旧卡片缩成模糊头像。

## 源动画与实际时序
SpriteFrames 中共 19 个动画项，其中 default 空、dog_interaction 含狗，17 个非空非狗动画可单独评估。逐项完整统计与所有帧的尺寸/包围盒见 JSON，不猜目录名称为动画。
- idle：8 帧，Godot speed 8，duration 权重 1，实际 125 ms/帧（8 fps）。
- 八向 walk_down / down_left / down_right / left / right / up / up_left / up_right：各 8 帧，speed 8、权重 0.8，实际 100 ms/帧（10 fps）。
- sleep_enter：13 帧，一次，100 ms/帧；sleep：4 帧，speed 2、权重 0.8，400 ms/帧（2.5 fps）。
- sleep_idle：8 帧，speed 3、权重 0.8，约 266.667 ms/帧（3.75 fps）；sleep_turn：16 帧，一次，100 ms/帧。
- badminton：8 帧，speed 7、权重 0.8，约 114.286 ms；handheld/music：各 16 帧，speed 6、权重 0.8，约 133.333 ms；dragon_ride：16 帧、100 ms；dog_interaction：16 帧、100 ms，明确排除双人角色库。

每帧毫秒 = 1000 × duration_weight / speed。非整数毫秒按累计时间四舍五入分配，例如 133/134 ms 交替补偿，不能每帧截断引入累积漂移。DSH 新男方 idle 使用 125 ms，步行 100 ms，休息可用 267/266 ms；双人动作共用同一动作时钟，不能各自自由随机计时。

## 锚点、方向与导出
源 Godot 为 centered=true、SPRITE_SCALE=1、NEAREST；没有显式“脚底锚点”。本项目根据 idle 的稳定底边定义直立锚点 (128,226)，不是谎称源中已有此字段。移动帧 256×256 保持原画布，不逐帧裁切/居中。部分活动源 AtlasTexture 为 253–255 宽/高，要按原 Rect2 抽取，再对称补透明边到 256；禁止重新划等分大图而忽略真实 Rect2。

躺姿接地锚点实测采用 (128,216)：sleep_idle 八帧 alpha≥128 的底边全部为 y=216，sleep_turn 为 216–217，sleep 为 215–217。1.0.1 修正了初稿暂定 250 导致的 34px 悬空偏差；旧快照保留，不再使用。sleep_enter 底边从 225/226 过渡到 211–215，P3 需要审核过渡锚点曲线；P1 只使用固定 216 的 sleep_idle，不声称入睡过渡已整合。不要把站姿脚底机械套在躺姿上，也不能用每帧 bbox 自动移动人物。P1 不伪造尚不存在的坐下/挥手/递信封动作；可以语义调度加 idle/方向姿态回退，运行界面须注明动作缺失。

DSH 男方要求：蓬松偏厚黑色中长刘海、深色眼睛、平静慵懒或呆萌神态、白色宽松 T 恤，略圆润的 Q 版身形。先交一张 256×256 原生像素概念和与 qianqian 并排的 1× 对照；直立可见高目标 192–204 px，轮廓粗细/像素颗粒/头身/阴影与参考图一致。真人照片仅本地受控路径，不进契约或公共提交；Codex 不再次付费生成。

DSH 交付单帧 frame_000.png 起，8-bit RGBA / sRGB / straight alpha，透明区域 RGB 清零。像素边界不得带缩放白边。可额外交 sheet，但不作为设备直接整张加载的入口；columns/rows/有效帧数/顺序均要明确，按行从左到右。每帧同一画布、接地锚点和尺寸，四周保留透明安全边。脚部不应因导出重采样漂移。

## 男方最小素材清单与验收
第一步仅静态：male.concept.front、male.concept.side；用户确认后制作 male.idle、male.walk 八向（每向 8 帧）、male.sleep_idle。第二步按联合设计补 male.wave / sit / letter_give / letter_receive / celebrate / touch / happy / sleepy。qianqian 对应动作缺项也需补，但不能用男方重绘替代 qianqian 原形。

新清单 schema littleworld.pixel-assets/1，字段：contract_version、asset_version、character_id、source_reference（不含照片本身/私密 URL）、animations。每套动作：id、direction、canvas [256,256]、anchor [x,y]、loop、frames[{path,sha256,duration_ms}]、sheet（可选）、known_limits、provenance。文件路径相对 Meowa 根，不用猜测生成任务的随机路径。候选/技术通过/用户确认/授权公开必须四项分开，DSH 的 accepted=true 不能替代 Codex 或用户验收。

## 场景、内存和不变边界
现代清爽 UI + 温暖低对比生活场景 + 原生细像素人物；不继续绘本动物方向，不用特别写实背景硬拼像素人物。旧 B_room_v1/C_home_v1 仅归档，先用明确标注的临时技术房间；正式房间需用户评审构图后交 DSH，背景不烘焙人物/卡片/文字。

256×256 RGB565+A8 为 196608 B/帧。两人当前帧 + 一人换帧峰值最低 589824 B；双人同时预备换帧则 786432 B，另算背景/LVGL/BSP/帧缓冲/字体/栈。不得把全部动作解码进内存。

P0 抽出的 113 帧未压缩像素共 22216704 B，作为 Windows 本地验证数据；不是可部署到现有 4 MiB storage 的固件资源包。现有 app 6 MiB、storage 4 MiB 不变，后续压缩/裁切/分批资源包需实测并核对最终容量，不能因 Flash 总标 32 MB 就认定都放得下。PSRAM 容量及可用堆以实证为准，当前不做板测。
