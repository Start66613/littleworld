Little World V4.1 TF 卡资源包
================================

把本目录下的 littleworld 文件夹整个复制到 TF 卡根目录，
插回设备后开机会自动校验并启用新的场景与角色动画。

目录结构（不要改名、不要改层级）：
  littleworld/littleworld-v4.json
  littleworld/scene-0.lwr  scene-0-thumb.lwr
  littleworld/scene-1.lwr  scene-1-thumb.lwr
  littleworld/scene-2.lwr  scene-2-thumb.lwr
  littleworld/male.lwp
  littleworld/qianqian-wave.lwp
  littleworld/littleworld-v4.sha256   （校验清单，可留在卡上）

说明：
1. 如果卡上已经存在 littleworld 文件夹，复制时会提示是否覆盖；
   建议先备份旧文件夹再覆盖。
2. 资源缺失或损坏时，设备会自动回退到内置画面，不会死机。
3. 校验方法：在电脑上运行 tools/verify_v4_sd.ps1 -Target <卡路径>。
4. 不要修改 littleworld-v4.json 里的 schema / version 数字。
