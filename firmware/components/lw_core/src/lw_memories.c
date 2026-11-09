/* SPDX-License-Identifier: MIT */
#include "lw/lw_memories.h"

/* Every entry is DEMO content. Replace the text (and later the photo paths)
 * with your own memories; the UI labels the page as DEMO while this set is used.
 */
static const lw_memory_t s_memories[] = {
    {{2024, 10, 25}, "故事开始", "示例内容：这一天我们在心里定下了一个小约定。把这里换成你们真正的第一天。", ""},
    {{2025, 2, 14}, "第一个情人节", "示例内容：一起吃了顿很慢的晚饭，然后在路灯下走了很久。", ""},
    {{2025, 6, 1}, "第一次一起看电影", "示例内容：电影其实一般，但两个人都记得回家的那条路。", ""},
    {{2025, 10, 25}, "一周年", "示例内容：蛋糕上的蜡烛点了一会儿就灭了，笑声比照片清楚。", ""},
    {{2026, 3, 8}, "春天的散步", "示例内容：公园里有一棵树，说好明年还要来看它。", ""},
    {{2026, 10, 25}, "两周年", "示例内容：小世界正式亮起来的一天。以后的回忆会继续加在这里。", ""},
    {{2026, 11, 8}, "一起布置小世界", "示例内容：把日常的小物一件件摆好，小世界慢慢有了家的样子。", ""},
    {{2026, 12, 25}, "冬天的热饮", "示例内容：两杯热饮冒着白气，窗外很冷，屋里很暖。", ""},
};

/* Daily short lines: one group of everyday / pet-flavoured sentences for
 * rotation displays. DEMO content in the same spirit as the tables above;
 * adding lines here changes no behaviour and no save format. */
static const char *const s_daily_lines[] = {
    "早安，今天也先伸个懒腰，再慢慢开始。",
    "饭要好好吃，步要慢慢走，日子要好好过。",
    "晒一会儿太阳，把心里的褶皱都晒平。",
    "忙完这一阵，记得抬头看看窗外。",
    "小家伙绕着脚边转了一圈，又乖乖趴好了。",
    "晚饭后一起散散步，风会把疲惫吹轻一点。",
    "今天的小愿望：早点休息，做个好梦。",
    "普通的一天也值得收好，明天再一起翻。",
};

static const lw_place_t s_places[] = {
    {"家", "温暖的起点", "示例场景：这里是你们每天回家会待的地方。以后可以放合照、留言和日常小物。"},
    {"回忆馆", "把日子收好", "示例场景：时间线、照片和信都会收在这里，一页一页翻过去。"},
    {"花园", "会长大的小地方", "示例场景：宠物喜欢在这儿晒太阳，春天会开出新的花。"},
    {"星星小屋", "晚上才开灯", "示例场景：夜里可以来看星星，也可以留一句悄悄话。"},
    {"惊喜盒子", "偶尔打开一次", "示例场景：里面放着一条小留言，每次打开都还在。"},
};

int lw_memories_count(void)
{
    return (int)(sizeof(s_memories) / sizeof(s_memories[0]));
}

const lw_memory_t *lw_memories_get(int index)
{
    if (index < 0 || index >= lw_memories_count()) {
        return 0;
    }
    return &s_memories[index];
}

int lw_places_count(void)
{
    return (int)(sizeof(s_places) / sizeof(s_places[0]));
}

const lw_place_t *lw_places_get(int index)
{
    if (index < 0 || index >= lw_places_count()) {
        return 0;
    }
    return &s_places[index];
}

int lw_daily_line_count(void)
{
    return (int)(sizeof(s_daily_lines) / sizeof(s_daily_lines[0]));
}

const char *lw_daily_line_get(int index)
{
    if (index < 0 || index >= lw_daily_line_count()) {
        return 0;
    }
    return s_daily_lines[index];
}

const char *lw_memories_welcome(void)
{
    return "这里收着我们一起走过的日子。\n慢一点翻，每一页都还在发光。";
}

const char *lw_surprise_message(void)
{
    return "今天也想告诉你：\n和你在一起的日子，\n是我最想反复过的那一种。";
}

const char *lw_surprise_hint(void)
{
    return "轻轻打开盒子，里面有一句话在等你。";
}
