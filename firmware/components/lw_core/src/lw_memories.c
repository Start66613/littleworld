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
