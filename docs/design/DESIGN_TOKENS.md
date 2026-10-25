# 设计 Token

实现依据 firmware/components/lw_ui/src/lw_os_design.c。三个 palette 的 bg/lower/surface/soft/text/muted/line/accent/deep/wood/sky 是系统唯一色彩入口；旧 App 保留旧 palette，不把旧主题编号混写成 OS 主题编号。

Sakura：背景 #FFF7EE，表面 #FFFCF8，文字 #51424A，辅助文字 #81737C，强调 #DA879D。Moonlight：背景 #222940，表面 #353C54，文字 #F7EFE6，辅助 #BEB8CE，强调 #BEAFD9。Pixel Home：背景 #F5F2DF，表面 #FBF9EE，文字 #3F514A，辅助 #78877A，强调 #8EA990。

字体：Noto Sans SC 子集，中文正文/控件 18，分区标题 22，页标题 28，纪念数字 44，锁屏数字 88。88 字体仅 ASCII；中文不强行使用缺字的数字字体。生成器扫描实际 UI/host 源码符号，复用 DSH 现有 D 盘字体源与 lv_font_conv，只读调用，不重新安装。授权 OFL 随工程保留。

栅格：页面边距 48，列间距 24/32；文字行距 6；控件间距 12/16。常规按钮最小 44px 高，主操作 50–60px；Dock 单项 104×80；设置行 260×44。滑块本体可窄，但扩展 16px 命中区域，总宽至少 44px。

圆角：图标底座 14，按钮 16，轻组件 22–24，系统面板 30–32。表面不透明度 246/255，遮罩 105/255，阴影宽 28、alpha18、偏移 y8；这是轻玻璃视觉替代，不做实时模糊。像素人物只原生 1×，关闭插值；主题缩略图允许缩略场景绘图，不缩小角色。

系统原型不显示假电池或未经实测的 PSRAM/存储余量。离线演示日期和 Windows Mock 都明确显示。
