# 联合视觉验收：当前未通过

2026-10-27，只读审查DSH `D:\LittleWorld-V43`，分支`agent/dsh-v4-3-engine`，HEAD `05bfc36b335a5a4636f38d641a0fefde4dbd6599`。它尚未包含冻结视觉提交2900569的祖先链。当前本机另有`integration/os-v4-p4-unified@3827040`，也不能替代尚未产出的新联合版。

实际检查`reports/ui-v2/os-v2`的首页、控制中心、宠物、RPG四张PNG；均为1024×600，与同名BMP的RGB逐像素一致；四张文件SHA均与旧V4.1目录完全相同。证据及路径见JOINT_EVIDENCE.json，审查副本在reviewed-dsh-old-ui。这些是实际旧LVGL画面，绝不是已通过的新联合版截图。

- 不是旧版UI：**未通过**，厚白Dock、大白纪念卡仍在。
- 桌面背景：存在三套既有美术中的首页，但不能证明新日常背景已接入。
- 男方与千千：旧首页外形可见，不能证明V4.3比例/七动作注册成功。
- 双人坐姿：**未通过**，宠物页仍为几何头像。
- 狗窝不漂浮：无法验收，组合狗窝尚未出现在这些截图中。
- 新UI组件：**未通过**，纸签导航、顶栏和组件版控制中心尚未出现。
- 中文：现有截图可读；冻结文楷字体是否正确合入仍待新截图。
- 页面返回：待真实联合版交互结果，静态旧图不能证明。
- 昼夜切图：待同一联合运行的日/夜及资源ID记录。
- 新遮挡/错位：待坐姿、狗窝、RPG新画面；不得用旧版画面判定通过。

V4.3 Dock文字拥挤登记为已知问题，本轮不重设计。Cream Glass属于后续V4.4，不阻塞首次显示成功。未做任何实板操作或验收。

DSH交回截图时应一并记录：联合分支HEAD、执行入口、资源清单SHA、实际加载的31个ID/缺失回退、时间源/phase/theme、是否真实板上。Windows PNG应保留对应BMP或帧缓冲来源；不要混用旧报告图片。建议输出`joint-home-day/night`、`joint-daily-seated`、`joint-dogbed-look/sleep`、`joint-control-center`、`joint-rpg`、`joint-story`、`joint-return-home`。
