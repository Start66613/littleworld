"""Render 1024x600 RPG preview frames from the C scenario snapshots.

This is a MOCK renderer: it draws the room, actors, quest panel and dialogue box
from the JSON lines produced by tools/rpg_scenario.c so the layout can be
reviewed without LVGL. It is not the game renderer.

Usage: python tools/render_rpg.py reports/rpg-scenario.jsonl reports/rpg
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 1024, 600
CREAM = (255, 248, 241)
PINK = (244, 165, 191)
LAV = (201, 181, 237)
SAGE = (191, 217, 200)
PEACH = (246, 207, 171)
INK = (74, 59, 69)
DIM = (142, 124, 136)
FLOOR = (240, 220, 200)
FLOOR_ALT = (235, 213, 192)
WALL = (250, 240, 230)

TILE = 44
GRID_X, GRID_Y = 32, 104
COLS, ROWS = 16, 8

FONT_PATHS = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\msyhbd.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
]


def font(size, bold=False):
    for path in ([FONT_PATHS[1], FONT_PATHS[0]] if bold else FONT_PATHS):
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


F_TITLE = font(26, True)
F_BODY = font(20)
F_SMALL = font(17)
F_NAME = font(19, True)


def tile_rect(tx, ty, pad=2):
    x = GRID_X + tx * TILE + pad
    y = GRID_Y + ty * TILE + pad
    return (x, y, x + TILE - pad * 2, y + TILE - pad * 2)


def draw_room(d, snap):
    d.rectangle((0, 0, W, H), fill=CREAM)
    d.rounded_rectangle((18, 84, 806, 512), 22, fill=WALL, outline=(232, 216, 204), width=2)
    # floor
    for ty in range(ROWS):
        for tx in range(COLS):
            blocked = (tx, ty) in BLOCKED
            color = (226, 206, 188) if blocked else (FLOOR if (tx + ty) % 2 == 0 else FLOOR_ALT)
            d.rectangle(tile_rect(tx, ty, 0), fill=color)
    # rug
    cx = GRID_X + 8 * TILE
    cy = GRID_Y + 4.5 * TILE
    d.ellipse((cx - 150, cy - 62, cx + 150, cy + 62), fill=(250, 226, 232), outline=(240, 208, 216))
    # furniture
    d.rounded_rectangle(tile_rect(2, 2, 1) + tuple(), 8, fill=(214, 226, 214), outline=(180, 200, 180))
    d.rounded_rectangle(tile_rect(9, 2, 1) + tuple(), 8, fill=(238, 222, 205), outline=(210, 190, 170))
    d.rounded_rectangle(tile_rect(9, 3, 1) + tuple(), 8, fill=(238, 222, 205), outline=(210, 190, 170))
    # hotspot markers
    for (tx, ty, label, color) in HOTSPOTS:
        x, y = GRID_X + tx * TILE + TILE // 2, GRID_Y + ty * TILE + TILE // 2
        d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=color, outline=INK)
        lf = F_SMALL
        tw = d.textlength(label, font=lf)
        ly = y - 30
        d.rectangle((x - tw / 2 - 5, ly, x + tw / 2 + 5, ly + 20), fill=(255, 255, 255))
        d.text((x - tw / 2, ly + 1), label, font=lf, fill=INK)


BLOCKED = {(2, 2), (3, 2), (9, 2), (10, 2), (11, 2), (9, 3), (10, 3), (11, 3)}
HOTSPOTS = [
    (5, 3, "照片盒", PINK),
    (9, 4, "信封", PEACH),
    (3, 3, "小盆栽", SAGE),
]


def draw_actor(d, tx, ty, kind):
    x = GRID_X + tx * TILE + TILE // 2
    y = GRID_Y + ty * TILE + TILE - 2
    body = PINK if kind == "qianqian" else (252, 252, 252)
    outline = (200, 120, 150) if kind == "qianqian" else (170, 170, 180)
    hair = (57, 56, 52)
    d.ellipse((x - 14, y - 5, x + 14, y + 4), fill=(226, 206, 190))
    d.rounded_rectangle((x - 12, y - 38, x + 12, y - 6), 8, fill=body, outline=outline, width=2)
    d.ellipse((x - 14, y - 62, x + 14, y - 34), fill=(252, 227, 210), outline=outline, width=2)
    d.chord((x - 15, y - 65, x + 15, y - 44), 180, 360, fill=hair)
    d.rectangle((x - 15, y - 54, x - 11, y - 48), fill=hair)
    d.rectangle((x + 11, y - 54, x + 15, y - 48), fill=hair)
    d.rectangle((x - 7, y - 52, x - 4, y - 48), fill=(42, 22, 18))
    d.rectangle((x + 4, y - 52, x + 7, y - 48), fill=(42, 22, 18))
    if kind != "qianqian":
        d.rectangle((x - 10, y - 32, x + 10, y - 18), fill=(255, 255, 255))


def draw_quest(d, snap):
    x0, y0 = 828, 96
    d.rounded_rectangle((x0, y0, x0 + 176, y0 + 300), 18, fill=(255, 255, 255), outline=(238, 224, 232), width=2)
    d.text((x0 + 16, y0 + 16), "任务本", font=F_TITLE, fill=INK)
    d.text((x0 + 16, y0 + 52), "整理回忆盒", font=F_BODY, fill=INK)
    items = ["照片", "信封", "小盆栽"]
    done = snap["progress"]
    for i, name in enumerate(items):
        yy = y0 + 92 + i * 40
        mark = "✓" if i < done else "○"
        color = (120, 170, 130) if i < done else DIM
        d.text((x0 + 16, yy), mark, font=F_BODY, fill=color)
        d.text((x0 + 44, yy), name, font=F_BODY, fill=INK if i < done else DIM)
    if snap["complete"]:
        d.rounded_rectangle((x0 + 12, y0 + 236, x0 + 164, y0 + 282), 12, fill=(255, 240, 226))
        d.text((x0 + 22, y0 + 244), "解锁：小相框", font=F_SMALL, fill=(200, 130, 90))
    else:
        d.text((x0 + 16, y0 + 244), "还差 %d 件" % (snap["total"] - done), font=F_SMALL, fill=DIM)


def draw_dialogue(d, snap):
    if not snap["dialogue"]:
        return
    x0, y0, x1, y1 = 32, 452, 806, 584
    d.rounded_rectangle((x0, y0, x1, y1), 18, fill=(255, 255, 255), outline=(201, 181, 237), width=3)
    # portrait
    d.rounded_rectangle((x0 + 16, y0 + 14, x0 + 116, y0 + 114), 12, fill=(252, 232, 238) if snap["speaker"] == "qianqian" else (240, 240, 246))
    pcx = x0 + 66
    pcy = y0 + 64
    d.ellipse((pcx - 30, pcy - 26, pcx + 30, pcy + 34), fill=(252, 227, 210), outline=(210, 170, 170), width=2)
    d.chord((pcx - 32, pcy - 32, pcx + 32, pcy + 8), 180, 360, fill=(57, 56, 52))
    d.rectangle((pcx - 32, pcy - 10, pcx - 22, pcy + 2), fill=(57, 56, 52))
    d.rectangle((pcx + 22, pcy - 10, pcx + 32, pcy + 2), fill=(57, 56, 52))
    d.rectangle((pcx - 14, pcy - 12, pcx - 6, pcy - 4), fill=(42, 22, 18))
    d.rectangle((pcx + 6, pcy - 12, pcx + 14, pcy - 4), fill=(42, 22, 18))
    name = snap["speaker"] or "旁白"
    d.text((x0 + 136, y0 + 16), name, font=F_NAME, fill=INK)
    # wrap text
    text = snap["text"]
    max_w = x1 - (x0 + 136) - 20
    lines, cur = [], ""
    for ch in text:
        if d.textlength(cur + ch, font=F_BODY) > max_w:
            lines.append(cur)
            cur = ch
        else:
            cur += ch
    if cur:
        lines.append(cur)
    for i, line in enumerate(lines[:3]):
        d.text((x0 + 136, y0 + 46 + i * 30), line, font=F_BODY, fill=(90, 76, 86))


def render(snap, out_path):
    img = Image.new("RGB", (W, H), CREAM)
    d = ImageDraw.Draw(img)
    draw_room(d, snap)
    draw_actor(d, snap["qx"], snap["qy"], "qianqian")
    draw_actor(d, snap["px"], snap["py"], "boy")
    draw_quest(d, snap)
    draw_dialogue(d, snap)
    d.text((32, 24), "《我们的故事》· 小屋", font=F_TITLE, fill=INK)
    d.text((32, 58), "RPG 竖切片 · mock 渲染（数据来自纯 C 核心）", font=F_SMALL, fill=DIM)
    img.save(out_path)
    print("rendered", out_path)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "reports/rpg-scenario.jsonl"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "reports/rpg"
    os.makedirs(out_dir, exist_ok=True)
    snaps = [json.loads(line) for line in open(src, encoding="utf-8-sig") if line.strip().startswith("{")]
    by_tag = {s.get("tag"): s for s in snaps}
    plan = [
        ("spawn", "01_room_spawn.png"),
        ("dialogue_photo", "02_dialogue.png"),
        ("photo_done", "03_progress_1of3.png"),
        ("quest_complete", "04_quest_complete.png"),
    ]
    for tag, name in plan:
        if tag in by_tag:
            render(by_tag[tag], os.path.join(out_dir, name))
    print("snapshots:", len(snaps))


if __name__ == "__main__":
    main()
