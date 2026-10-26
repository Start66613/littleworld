# 字体落地规格

主字体源：[LXGW WenKai Screen v1.522](https://github.com/lxgw/LxgwWenKai-Screen/releases/tag/v1.522)。选择屏幕阅读版的较饱满笔画，保留手写的温和感，不采用低分辨率点阵小字。主字体只用一套，18px正文/状态、22px按钮/小标题、28px主标题；数字时钟44/88px保留Noto Sans SC，避免大数字笔画过轻。

官方下载字体 SHA-256：`cd1a6fa39c4ea42fd8f4e289945789b0e510cf7016435640f8893cdad9b220f3`。固定下载脚本 `tools/setup_scene_font.ps1`；不安装系统字体，不覆盖用户软件。SIL OFL保留于 `firmware/components/lw_ui/fonts/OFL-WenKai.txt`，原Noto许可继续保留。

源TTF仅开发机缓存，未进入固件运行文件系统；`gen_fonts.ps1` 扫描全部共用UI/Host源，当前578个非ASCII字符加现有ASCII范围，导出4bpp压缩LVGL C位图。生成对象名继续为 `lw_font_18/22/28`，没有改业务调用接口；设备只编译这些子集。C源大小不是运行RAM占用，最终Flash/PSRAM/XIP由硬件构建配置决定。

真实例图：`screenshots/v43-time-settings.png`（18/22/28层级）、`28-story-detail.png`（中文长句）、`v43-daily-day.png`（状态与触摸按钮）。没有用图片烘焙动态中文文字。
