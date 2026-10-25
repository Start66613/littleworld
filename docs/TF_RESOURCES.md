# TF 资源基础设施 v1

实现为 lw_storage（不依赖LVGL）→ 标准C stdio/VFS → Windows目录或ESP FATFS。Windows当前像素包的 read provider 已实际接入，不是Mock。每帧只读取所需offset/bytes，后台单worker提前读下一帧；最多4个帧buffer、8个请求，重复请求去重。未一次展开整包22.2MB。

缓存READY帧按LRU淘汰；在途buffer计入同一上限。mutex只保护索引/拷贝，不在文件I/O期间持锁。遇到同帧在途读取时，前台可直接读自己的目标，可能重复I/O但不等待worker锁或另增缓存buffer。容量不足/PSRAM分配失败退回直接读取；缺文件或截断返回失败，由现有sprite保留上一有效帧/初始回退。invalidate按代数丢弃旧在途结果；close清队列、join，再释放句柄与内存。关闭后才能卸载卡，不宣称支持任意时刻拔卡。

Windows用真实CreateThread/事件/临界区；ESP用FreeRTOS任务/信号量，帧buffer只用heap_caps_malloc(SPIRAM|8BIT)。小控制结构仍在普通堆，ESP worker栈4096B；Windows线程虚拟保留不计入像素预算。API统计分别记录缓存、元数据和LVGL像素，不能相加后当作整机PSRAM实测。

目录约定：SD卡已有FAT32分区，/sdcard/littleworld/characters、scenes、album、music、rpg；模拟器由明确指定的本地根目录映射。当前pack读取只访问指定文件，不扫描照片目录。相对路径限ASCII字母数字、下划线、短横线、点与层级斜线，拒绝绝对路径和..。根目录由调用方信任，不把此检查冒充防御恶意symlink的沙箱。单帧最大4MiB、单次offset范围限2GiB；大文件/视频另设格式，当前不是通用媒体解码器。

## 官方依据与SD适配

固定官方仓库9fcd49a8c927a00f5583c22d568b3448addbe51c，实际路径examples/esp-idf/04_sdmmc/main/sd_card_example_main.c、main/Kconfig.projbuild。7B BSP3.0.1的bsp_sdcard_mount使用SDMMC slot0、4bit、LDO4；官方头声明D0=39,D1=40,D2=41,D3=42,CMD=44,CLK=43。适配直接调用BSP，不另猜引脚、不重配C6。

lw_sd_mount_existing显式调用已有分区挂载；构建若定义CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL则#error拒绝。lw_sd_asset_root只返回路径，不创建/格式化目录。原app_main不调用挂载函数。FatFs支持的FAT32读取由官方VFS提供，本轮没有卡，不会格式化、插拔或速度测试。BSP当前挂载失败后的LDO清理由官方实现决定，未来实板接入前仍需检查重复挂载/失败重试的生命周期。

## 内存预算（256² RGB565+A8）

每帧196608B；两名角色当前帧393216B，单次替换时LVGL帧峰值589824B。无缓存增量上限589824B；2帧缓存上限983040B（0.9375MiB）；当前4帧缓存上限1376256B（1.3125MiB），另加元数据、worker、文件系统、字体、绘图和显示buffer。8帧缓存若未来扩展槽数，上限2162688B（2.0625MiB），当前代码并未实现8槽。

一张1024×600 RGB565背景1228800B，带A8为1843200B；当前786432B预算装不下，直接读/单独背景预算，绝不能按4张高清背景缓存。本轮场景由LVGL绘图，无全屏图片解码成本。实际PSRAM容量/可用量与XIP资源占用须在未来硬件授权后测量。
