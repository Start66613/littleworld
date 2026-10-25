# 像素素材交接与待补项

2026-10-25；Codex。当前男方像素候选尚未交付，未作通过或不通过判定。不能将“生成成功”、旧 schema/1 的 accepted=true 或 TEST A 占位视作正式验收。

## 已核验的实际文件
- 女方原图：D:\Espressif\LittleWorld\sources\DesktopPet\assets\sprites\idle\idle_01.png；256×256，人物可见 198 px、脚底 y226。它是匹配男方的实际依据。
- 女方动作：同仓库 assets/runtime/pet_sprite_frames.tres，真实 AtlasTexture Rect2、方向与帧时已解析；不直接导入 .tres 到 LVGL。
- 旧生产清单：D:\LittleWorld-Meowa\handoff\ASSET_MANIFEST.json；仍为 littleworld.assets/1 的绘本批次，没有男方像素文件。
- 旧角色：concepts/A1_cream_mochi/result_images_0_url.png、concepts/A2_moss_sprite/result_images_0_url.png、concepts/A3_scarf_foxcat/result_images_0_url.png（均相对 D:\LittleWorld-Meowa）。按用户新方向归档，不作主角，不要求付费重做这些动物。
- 旧房间 rooms/B_room_v1/result_images_0_url.png 与首页概念 concepts/C_home_v1/result_images_0_url.png：保留。水彩纹理与烘焙角色/大面板不符合本轮原生像素双人界面，不能作为正式运行背景直接接入。

## DSH 下一次交付
请先在 handoff/pixel/ASSET_MANIFEST.json 与 ASSET_REPORT.md 记录新批次（这是约定的交付位置，当前尚不存在）。不要覆写旧批次，不把真人参考照片或带密钥的日志放入交接包。

1. 男方静态正面和侧面概念：256×256、透明、可见高 192–204 px，脚底 (128,226)，黑厚刘海/深眼/白色宽松 T 恤。先提交与 qianqian 原图 1× 并排的对照图，标明实际文件及 SHA-256；未获用户形象确认前不批量生成动作。
2. 比较报告逐项填写像素颗粒、1px轮廓、头身、暗部层级、眼神/表情和可见高度。若男方画布是 1024 再缩成 256，必须证明细节仍落在原生像素格上；不得用双线性缩小来掩饰不一致。
3. 确认后交 idle 8 帧/125ms、八向 walk 各8帧/100ms、sleep_idle 8 帧/累计补偿约267ms，并逐帧记录锚点。其他情绪按契约扩展；不要只给同一表情重复帧。
4. P3 需要双方 wave、sit、letter_give/receive、look_window、celebrate、touch。qianqian 当前没有这些独立动画名；现有 music/handheld 含道具，不能直接冒充双人挥手或递信。先确定共同时长、站位和手部接触点，再单独生产。
5. 正式房间另交无角色、无烘焙 UI/文字的 1024×600 背景；暖色细像素或相容的简洁场景，不能特别写实。预留脚底活动带 x168–856/y336–466、双人并排和躺姿宽度。房间/家具交互锚点附 JSON；别为了旧水彩背景牺牲风格统一。

## 收到真实男方文件后的判定格式
每张写真实相对路径、版本/哈希、技术尺寸/alpha 结果、与 qianqian 对照差异、明确整改目标（如人物可见高 240→约198px、描边3px→1–2px等），以及是否需用户审美确认。这里的数值例子不是对未到货素材的指控。

DSH 维护素材/清单/生产报告；Codex 维护本返工文档与独立反馈快照。当前未调用付费生成、未批准任何正式男方资源公开。
