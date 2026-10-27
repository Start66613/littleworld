Little World V4.3 TF 卡资源包
================================

把本目录下的 littleworld 文件夹整个复制到 TF 卡根目录，
插回设备后开机会自动校验并启用新的场景与角色动画。
最终卡上的路径必须是   /sdcard/littleworld/  （littleworld 并列于卡根，
不要再套一层 littleworld，即不存在 /sdcard/littleworld/littleworld）。

目录结构（不要改名、不要改层级）：
  /sdcard/littleworld/littleworld-v4.json        （清单，固件先读它）
  /sdcard/littleworld/scene-0.lwr  scene-0-thumb.lwr
  /sdcard/littleworld/scene-1.lwr  scene-1-thumb.lwr
  /sdcard/littleworld/scene-2.lwr  scene-2-thumb.lwr
  /sdcard/littleworld/male.lwp              （男方 7 段动画）
  qianqian-base.lwp 不在本包内：它含私有素材，只打进私有包。
  没有它时千千用内置待机帧 + 招手包，画面正常但动作少。
  /sdcard/littleworld/qianqian-wave.lwp     （千千招手）
  以下为可选，缺了不会崩，只是少一层美术：
  /sdcard/littleworld/daily.lwp             （双人坐姿与狗窝 8 段）
  /sdcard/littleworld/ui.lwp                （Meowa 组件图集）
  /sdcard/littleworld/skin.lwp              （17 种皮肤的图集）
  /sdcard/littleworld/daily-room.lwr  daily-room-day.lwr
  /sdcard/littleworld/daily-room-night.lwr  rpg-room.lwr
  /sdcard/littleworld/littleworld-v4.sha256  （校验清单，可留在卡上）

说明：
1. 如果卡上已经存在 littleworld 文件夹，复制时会提示是否覆盖；
   建议先备份旧文件夹再覆盖。
2. 资源缺失或损坏时，设备会自动回退到内置画面，不会死机。
3. 校验方法：在电脑上运行 tools/verify_v4_sd.ps1 -Target <卡路径>。
4. 不要修改 littleworld-v4.json 里的 schema / version 数字。
