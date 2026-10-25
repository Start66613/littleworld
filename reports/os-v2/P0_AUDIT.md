# OS 工程与环境审查

2026-10-25。用户请求载于 Little World OS.docx，SHA256 1220f30c867c568176cdb9966b6a1f00c01f3ea664b5c08ca825a4f332a914b1。只读提取其开发要求，没有修改原文档，也没有把 DSH 报告中的预算/公开判断当作本轮授权。

## 工程实况

实际存在 C:\Projects\LittleWorld、D:\LittleWorld-Main-DSH、D:\LittleWorld-Meowa、D:\LittleWorld-Codex。早期C盘工程不作为工作位置。DSH主工作树为main、稳定提交8af0637；Codex原分支agent/pixel-duo在afa3233完成P0/P1基础。已从它建立agent/littleworld-os-v2，不覆盖main、art/batch-1或DSH工作树。

正式 Little World 仓库 https://github.com/Start66613/littleworld；DesktopPet https://github.com/Start66613/DesktopPet，固定 b00a7005f4889644737092dbc7379e2a9b011315。前一轮已实际获取/读取，本轮复查本地状态并复用逐帧导出。保留 lw_core、arcade_core、存档、日期、小游戏及旧入口。OS原型通过新入口使用同一真实LVGL库。

现有相册是带占位内容的基础浏览UI；音乐是模拟播放UI，不是已验证音频输出。既有小游戏算法保留。旧白色卡片式shell作为稳定回归入口，新UI独立，不将已有基础逻辑错误描述为外设全部正常。

## Meowa 已有安装与免费验证

实际技能：D:\LittleWorld-Meowa\tools\meowa-skills\skills\game-assets\SKILL.md；官方仓库 https://github.com/Meowa-AI/meowa-skills，安装提交33578ab708c1b4dfcd83fe79dffdbd47ff4083b2。CLI meowart_api.py版本2026.10.23.2，Python3.12.14/Pillow13.0.0在原有D盘venv。通过既有meowa.ps1在原认证目录调用credits-balance，认证连通，总余额2904（paid2110/subscription600/trial194）。没有打印/复制密钥，也未重新安装。

已读取实际general-image-run --help，支持官方Image2.5/Image2/Nano Banana公开选项。当前官方技能标示Image2.5普通1K/2K估算1/2积分，精细5/10、极致10/20，每参考图+2；这些是已安装官方说明的估算，不冒充本轮服务端结算。DSH batch2实际记录custom-size256按2K收20积分/纯文本，共3次60。CLI未提供本轮零提交的服务器结算账单，不能以小尺寸猜费用。

本轮美术清单为原创代码房间三套、统一程序图标、已存在qianqian帧、测试人偶，费用0；无新增生成job、无新预算申请、无真人照片上传。若后续需要生成正式背景，会先列资产/单价/总预算，获用户批次批准后才提交；不继承DSH此前的授权。

## BSP 与真实硬件边界

官方代码固定9fcd49a8c927a00f5583c22d568b3448addbe51c；本地D:\Espressif\LittleWorld\sources\waveshare-7b。工程使用IDF5.5.5、esp32p4 rev3_x/最小300、32MB，7B BSP3.0.1→esp_lvgl_adapter0.6.4→LVGL9.5.0，无旧esp_lvgl_port并列集成。

BSP源码实际提供bsp_display_brightness_set及backlight_on/off，使用LEDC百分比。lw_system_bsp_backend只提供显式调用的适配工厂，不初始化硬件、不更改稳定入口；服务正常调光限制最低10%，背光关闭应走单独电源策略。

扬声器初始化使用ES8311，BSP通过esp_codec_dev返回输出codec；音量使用esp_codec_dev_set_out_vol，录音路径另用ES7210。当前没有确认外接喇叭/音频链路，所以Windows音量是模拟，构建不能证明真实声音。适配工厂要求调用方已有正确准备的codec句柄，不擅自开启功放或更新C6。

官方examples/esp-idf/05_wifistation实际依赖esp_wifi_remote==1.2.5和esp_hosted1.4.*，应用层用esp_wifi_*及WIFI/IP事件；P4通过C6远端无线，不当作P4片内Wi-Fi。所查defaults未显式固定完整传输/引脚配置，后续须核对实际hosted组件、板级线路和C6现有固件协议；本轮不猜SDIO/SPI引脚，不为显示网络列表而添加或更新无线固件。

backend预留scan/connect/disconnect与异步扫描/连接回调；未接入真实网络时能力为unsupported，不能显示真实已连接。Windows使用固定模拟AP，未扫描宿主Windows网络。SD、NVS设置持久化、网络校时与实体唤醒待后续接入；不显示虚假电量、存储余量或PSRAM容量。

## 新到角色

本轮已实际查看DSH pair_256和boy_front_aligned原图，测量并输出docs/handoff/DSH_ART_TASKS.md。男方215px/64色、女方198px/716色，并存在头身、粗细和明暗差异，尚未达到同游戏标准；不能据DSH“采用/可公开”自评导入正式场景或上传。测试人偶不是这张候选。原图片和私人素材都保留本地。
