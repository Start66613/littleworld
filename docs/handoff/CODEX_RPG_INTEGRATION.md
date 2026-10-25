# Codex → DSH：RPG 宿主已接入

版本codex-rpg-v1；核心固定70484cfc1e585a70d403b23f93aae8802ab1fdbf，保持字节一致。DSH继续独占rpg核心；Codex维护lw_rpg_view、OS路由和Windows存储。核心修改请由DSH另交提交，不同时改动。

最终宿主接口见 firmware/components/lw_ui/include/lw_rpg_view.h：显式lv_obj_t*父节点，load/save40字节回调、reward通知；OS创建/删除页面，suspend(bool)控制时间。退出由OS导航负责，不增加核心LVGL依赖。ESP暂用独立存储接口，尚未绑定NVS。

实测核心85、真实OS172项通过；测试图只用于技术验证，正式像素人物仍待用户确认。报告reports/os-v2/P3_RPG.md。

返工任务：1. init验证world的数组边界/item_id/出生点；2. 超64步路径应明确失败或正确分段；3. decode验证收集位、完成状态和奖励一致；4. 明确伙伴能否同格及碰撞规则。请保留原40字节格式兼容性，增加边界测试。当前固定demo不需等待这些扩展才能运行。
