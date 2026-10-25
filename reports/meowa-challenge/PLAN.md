# Codex 单次 Meowa 对照实验

2026-10-25，独立分支 agent/codex-meowa-challenge。用户授予独立预算硬上限40积分，本次仅1次提交；无动画、无真人照片、无自动重新付费提交。首次生成前未查看DSH rpg/meowa-v3 新一轮的最终提示词或结果。

官方Skill/CLI安装33578ab708c1b4dfcd83fe79dffdbd47ff4083b2、2026.10.23.2，查询官方HEAD一致。已读pixel-gen-run/custom-size-pixel-gen-run/asset-clone-run/image-edit-run/one-click-upgrade-run/character-multi-view-run文档和实际--help。认证余额读取2704；其他代理同时使用账户，余额差可能包含别的任务，最终扣费以本任务可核验信息为准。

实时模板目录已查询：large_portrait为256px、2个生成槽、2K、默认advanced抠图；pixel_char_1/2/6为128px、2个槽、1K；general_character_64px为64px、8个槽。预设固定尺寸与数量不能靠提示词改写。完整公开产品目录可由官方pixel-gen-template-info复现，不下载或复用DSH结果。

方法比较：pixel-gen适合固定模板，但另一个模板会引入额外风格；custom-size可指定256，但参考引导和填满画布可能改变比例；asset-clone适合同风格替换，默认附带多结果/像素处理，本次无需多个候选；one-click-upgrade适合保留同一身份的变体；multi-view要求先定稿，不适合首次人物确认。本次选image-edit-run pixel strict，用原版站姿约束像素结构，直接改变性别/发型/服装，保留原图为对照，不替换正式女方。

实际选项：nano-banana、1K、pixel、strict、remove-bg-method none、no-multi-pixelation、1张非真人的原版qianqian参考。官方当前价格说明Nano Banana1K10积分、每参考+2，未启用regional+2或付费去背景；预计12，小于40。只本次一次收费请求，不因结果不好重试；下载失败只轮询原job。

提示词：Turn this sprite into her matching boy companion: fluffy dark hair with long naturally parted bangs, warm dark eyes and a gentle cheerful smile; loose white T-shirt with dark collar trim, dark shorts and sneakers. Keep the standing pose, approximately 2.5-head proportions, 198px visible height on the 256px canvas, warm charcoal outlines, refined pixel clusters and soft shading. A single full-body boy for frame animation.

原始结果/男方测试图可公开。本轮用户明确选择：含原版Qianqian的并排图、房间合成图及运行截图仅本地，且所有真人照片/密钥/存档不公开。未对既有公开Git历史进行重写。
