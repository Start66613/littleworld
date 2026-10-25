# Meowa Skill 完整同步与验证

日期：2026-10-26，Asia/Shanghai。付费生产前已完成本报告的检查。

- CODEX_HOME环境变量未设置，默认用户目录为C:\Users\ASUS\.codex；常见skills/game-assets目录实际不存在。正在使用的完整官方Skill在D:\LittleWorld-Meowa\tools\meowa-skills\skills\game-assets。
- 原/新CLI均为2026.10.23.2。没有伪造版本升级：实际fetch官方仓库并执行merge --ff-only后提示Already up to date，当前本身已与最新官方main一致。
- 官方remote：https://github.com/Meowa-AI/meowa-skills.git；固定SHA：33578ab708c1b4dfcd83fe79dffdbd47ff4083b2。完整目录包含SKILL.md、meowart_api.py/.md、references、scripts、assets、agents和版本/参数契约，非单文件替换。
- 更新前完整备份：D:\LittleWorld-Private\skill-backups\game-assets-20261026-33578ab。备份不加入项目Git。
- Git工作树干净；发布文件校验值按LF计算。Windows checkout为CRLF，规范化换行后的runner SHA-256与官方release.json一致，未修改runner。
- Python3.12.14、requests2.34.2、Pillow13.0.0；--version、顶层--help、credits-balance均成功。初始可用2704（付费2110、订阅584、当日体验10）；余额查询可能按官方规则领取当日体验积分。
- 认证原样复用，未创建/替换账号或Key，未复制到新工程、未输出私有配置。C盘不新增Skill副本。
- 已查阅官方SKILL、CLI说明、capability-routing、pixel-and-hd-assets、ui-and-image-editing、animation-and-video、running-and-outputs，实际查询本次所用命令的--help。通用生成采用general-image-run；动画按细微待机选V1、挥手选V2，输出逐帧而非把视频当游戏动画。
- 官方明确价格：Image2.5 2K Ultimate基础20+每参考图2；V1 Standard动画30+抠图档位；V2按帧数/质量/抠图批量计算，提交前核对完整报价。中断用原Job ID的poll恢复。

结论：完整官方Skill同步/校验和认证验证成功，可以在本轮1000积分硬上限内执行已授权生产。官方地址、文档和本地工具均固定在上述提交，不使用代理Runner或远程替换脚本。
