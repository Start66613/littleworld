# 首次独立评价冻结后的 DSH 对照

生成结束且 REPORT.md 独立评价写完后，才读取 DSH art/meowa-c1-pair 的结果及指标。没有据其提示词追加生成。DSH 文件仅在原工作区只读访问，未复制到公开素材目录。

本机按相同 alpha≥128 包围盒口径重测：原版98×198、DSH A 113×220、DSH B 120×221、Codex104×208。DSH原报告221/222是包括低alpha边缘的不同口径，不能混在同一排名里。DSH A/B分别有1287/1330个半透明像素，本地去背景的Codex为二值alpha；这不表示Codex边缘一定更自然。

DSH A/B不透明像素478/338色，Codex4889色，原版716色。查看本地原生1×对比后：DSH A偏冷的白衣和紫黑头发，DSH B像素簇更粗；Codex偏暖配色、眼神和圆润轮廓更接近原版，但仍偏高，且近邻颜色碎片明显。DSH两图都比原版高22–23px，Codex高10px。身高距离和色板距离给出了不同结论，不能只选对自己有利的指标。

对比图 D:\LittleWorld-Codex-Challenge\private\meowa-challenge\comparison-dsh-codex-1x.png（1120×304）已实际查看：脚底统一，均1×，只平移不缩放；包含原版Qianqian，仅本地。免费脚本 tools/compare_meowa_candidates.py 和公开数字comparison-metrics.json可复现。女方始终使用原版，DSH生成女方不作替代。

我的判断：Codex单次方案值得保留为更接近原版比例/暖灰色系的候选，DSH模板方案在调色板收敛上更好；两者都没有通过最终画风和动画验收。最终选择交给用户，本轮停止所有付费重试，继续系统开发。
