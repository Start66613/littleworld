# P4：实际文件读取与缓存

Windows真实实现：lw_storage的文件slice、LRU上限、单后台worker预读、按代失效与退出join；已接host/pixel_pack.c读取原版角色帧。不是只写接口，也没有用Mock内容替代文件读取。ESP-IDF组件提供同源FreeRTOS/PSRAM实现和禁止自动格式化的官方BSP挂载适配；最终离线编译通过，未运行硬件。最终StorageTests句柄73→73，先前77→77同样是通过记录。

33项StorageTests通过，含真实二进制内容/offset、命中避免I/O、后台预读命中、LRU保留近期帧、超预算旁路、缺文件、截断、路径/offset边界、改文件后失效、并发失效、100次立即关闭在途worker。Windows进程句柄77→77。首轮/WX发现worker局部key可能未初始化，已显式初始化。还修正pending统计覆盖已出队但未开始读的任务，避免把短暂空隙当作worker空闲。

接实际角色文件后OS174项通过；像素lease峰值589824B，缓存峰值786432B，统计包含在途缓存。一次记录hits920、prefetched652、evictions733；这些计数依调度变化，最新以runtime.json/storage为准，不当作TF性能。缓存元数据另列，不宣称整机内存为这两项之和。

限制与复现详见docs/TF_RESOURCES.md。首次读取/缓存miss仍可能同步阻塞文件I/O；无保证帧率的解码管线、无音频解码、无SD热插拔检测、无实板TF吞吐/稳定性结论。应用内容与正式资源包部署仍待后续，原固件入口/分区保持不变。
