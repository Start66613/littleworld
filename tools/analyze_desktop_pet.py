"""Read Godot SpriteFrames, measure original art, export local LVGL test frames.
No Godot evaluation, network, AI redraw or writes to the source checkout.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
from PIL import Image, ImageDraw


def parse_frames(root):
    text = (root / "assets/runtime/pet_sprite_frames.tres").read_text(encoding="utf-8")
    ext = {rid: path.removeprefix("res://") for path, rid in re.findall(
        r'\[ext_resource type="Texture2D" path="([^"]+)" id="([^"]+)"\]', text)}
    sub = {}
    for rid, block in re.findall(r'\[sub_resource type="AtlasTexture" id="([^"]+)"\]([^\[]+)', text):
        atlas = re.search(r'atlas = ExtResource\("([^"]+)"\)', block)
        region = re.search(r'region = Rect2\(([^)]+)\)', block)
        if atlas and region:
            sub[rid] = (ext[atlas[1]], tuple(int(float(v.strip())) for v in region[1].split(",")))
    clips = []
    pattern = r'\{\s*"frames":\s*\[(.*?)\],\s*"loop":\s*(\w+),\s*"name":\s*&"([^"]+)",\s*"speed":\s*([\d.]+)\s*\}'
    for frames, loop, name, speed in re.findall(pattern, text, re.S):
        clip = {"name": name, "speed": float(speed), "loop": loop in ("1", "true"), "frames": []}
        for duration, kind, rid in re.findall(r'"duration":\s*([\d.]+),\s*"texture":\s*(ExtResource|SubResource)\("([^"]+)"\)', frames):
            path, region = (ext[rid], None) if kind == "ExtResource" else sub[rid]
            clip["frames"].append({"source": path, "region": region, "duration_weight": float(duration),
                                   "duration_ms": round(1000 * float(duration) / float(speed))})
        cumulative = 0.0
        previous_ms = 0
        for frame in clip["frames"]:
            cumulative += 1000 * frame["duration_weight"] / clip["speed"]
            end_ms = round(cumulative)
            frame["duration_ms"] = end_ms - previous_ms
            previous_ms = end_ms
        clips.append(clip)
    if not clips or not next((c for c in clips if c["name"] == "idle" and len(c["frames"]) == 8), None):
        raise ValueError("Unexpected SpriteFrames layout; parser refuses guessing")
    return clips


def original(root, frame):
    path = (root / frame["source"]).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("source escapes repository")
    with Image.open(path) as source:
        image = source.convert("RGBA")
    if frame["region"]:
        x, y, width, height = frame["region"]
        if x < 0 or y < 0 or x + width > image.width or y + height > image.height:
            raise ValueError("atlas rectangle exceeds original image")
        image = image.crop((x, y, x + width, y + height))
    return image


def rgb565a8(image):
    rgba = list(image.get_flattened_data())
    rgb = bytearray(len(rgba) * 2)
    alpha = bytearray(len(rgba))
    for i, (r, g, b, a) in enumerate(rgba):
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) if a else 0
        struct.pack_into("<H", rgb, i * 2, value)
        alpha[i] = a
    return rgb + alpha


def analyze(root, output, public_report):
    output.mkdir(parents=True, exist_ok=True)
    clips = parse_frames(root)
    all_stats = []
    runtime = {"schema": "littleworld.pixel-pack/1", "character": "qianqian", "canvas": [256, 256], "clips": []}
    selected = {"idle", "walk_left", "walk_right", "walk_up", "walk_down", "walk_up_left", "walk_up_right",
                "walk_down_left", "walk_down_right", "sleep_enter", "sleep_idle", "sleep_turn", "sleep"}
    contact = Image.new("RGB", (4 * 256, 6 * 288), "#fbf5ec")
    draw = ImageDraw.Draw(contact)
    references = []
    foot_samples = []
    for clip in clips:
        sizes, boxes, solid_boxes, durations = [], [], [], []
        alpha_values = Counter()
        for frame in clip["frames"]:
            image = original(root, frame)
            sizes.append(list(image.size))
            alpha = image.getchannel("A")
            boxes.append(alpha.getbbox())
            solid = alpha.point(lambda a: 255 if a >= 128 else 0).getbbox()
            solid_boxes.append(solid)
            alpha_values.update(alpha.get_flattened_data())
            durations.append(frame["duration_ms"])
            if clip["name"] == "idle" and solid:
                foot_samples.append({"solid_bottom_exclusive": solid[3], "solid_bbox": solid})
        stat = {"name": clip["name"], "frames": len(sizes), "godot_speed": clip["speed"], "loop": clip["loop"],
                "duration_ms": durations, "effective_fps": round(len(durations) * clip["speed"] / sum(f["duration_weight"] for f in clip["frames"]), 3) if durations else 0,
                "source_canvas_sizes": sorted(set(tuple(s) for s in sizes)), "alpha_bbox_per_frame": boxes,
                "solid_bbox_per_frame": solid_boxes, "opaque_height_range": [min((b[3]-b[1] for b in solid_boxes if b), default=0), max((b[3]-b[1] for b in solid_boxes if b), default=0)],
                "alpha_levels": len(alpha_values), "exclusion": "contains independent dog companion; not a male/female duo clip" if clip["name"] == "dog_interaction" else None}
        all_stats.append(stat)
        if clip["name"] in selected and clip["frames"]:
            entries = []
            for index, frame in enumerate(clip["frames"]):
                image = original(root, frame)
                if image.width > 256 or image.height > 256:
                    raise ValueError("Oversized selected frame needs an explicit conversion decision")
                # Match Godot centered=true with deterministic padding. No rescale,
                # individual trim, anatomy redraw or per-frame recentering.
                normalized = Image.new("RGBA", (256, 256))
                offset = ((256 - image.width) // 2, (256 - image.height) // 2)
                normalized.paste(image, offset)
                target = output / "frames" / clip["name"]
                target.mkdir(parents=True, exist_ok=True)
                raw = target / f"frame_{index:03}.rgb565a8"
                raw.write_bytes(rgb565a8(normalized))
                png = target / f"frame_{index:03}.png"
                normalized.save(png)
                entries.append({"file": raw.relative_to(output).as_posix(), "duration_ms": frame["duration_ms"],
                                "source": frame["source"], "region": frame["region"], "padding": offset,
                                "sha256": hashlib.sha256(raw.read_bytes()).hexdigest()})
            runtime["clips"].append({"name": clip["name"], "loop": clip["loop"], "frames": entries})
        if clip["frames"] and clip["name"] not in ("default", "dog_interaction"):
            references.append((clip["name"], clip["frames"][0]))
            if clip["name"] in ("idle", "sleep_enter", "sleep_turn"):
                references.append((clip["name"] + " / last", clip["frames"][-1]))
    for i, (name, frame) in enumerate(references[:24]):
        image = original(root, frame)
        x, y = (i % 4) * 256, (i // 4) * 288
        contact.paste(image, (x + (256-image.width)//2, y), image)
        draw.text((x+8, y+262), name, fill="#574e60")
    rows = (min(24, len(references)) + 3) // 4
    contact.crop((0, 0, 1024, rows * 288)).save(output / "qianqian-reference.png")
    runtime["source_commit"] = "b00a7005f4889644737092dbc7379e2a9b011315"
    runtime["raw_frame_bytes"] = 196608
    runtime["raw_storage_bytes"] = sum(len(c["frames"]) for c in runtime["clips"]) * 196608
    # Self-contained raw seek pack: 16-byte header, 108-byte records, then frames.
    # Host reads only metadata and one requested frame. No PNG/Godot dependency.
    records = bytearray()
    payload = bytearray()
    offset = 16 + 108 * len(runtime["clips"])
    for clip in runtime["clips"]:
        anchor_y = 216 if clip["name"] in ("sleep", "sleep_idle", "sleep_turn") else 226
        times = [f["duration_ms"] for f in clip["frames"]]
        records.extend(struct.pack("<32sHhhBBI32H", clip["name"].encode("ascii"), len(times), 128, anchor_y,
                                   int(clip["loop"]), 0, offset, *(times + [0] * (32 - len(times)))))
        for frame in clip["frames"]:
            data = (output / frame["file"]).read_bytes()
            payload.extend(data)
            offset += len(data)
    (output / "qianqian.lwpx").write_bytes(struct.pack("<8sHHHH", b"LWPX0001", 256, 256, len(runtime["clips"]), 0) + records + payload)
    (output / "pack.json").write_text(json.dumps(runtime, ensure_ascii=False, indent=2), encoding="utf-8")
    idle = original(root, next(c for c in clips if c["name"] == "idle")["frames"][0])
    colors = Counter((r,g,b) for r,g,b,a in idle.get_flattened_data() if a >= 128)
    report = {"source": "https://github.com/Start66613/DesktopPet", "commit": runtime["source_commit"],
              "godot_scene": "scenes/pet.tscn", "script": "scripts/pet.gd", "sprite_frames": "assets/runtime/pet_sprite_frames.tres",
              "animations": all_stats, "idle_foot_samples": foot_samples, "idle_opaque_rgb_colors": len(colors),
              "idle_top_colors": [{"rgb": list(rgb), "pixels": n} for rgb,n in colors.most_common(12)],
              "runtime_raw_storage_bytes": runtime["raw_storage_bytes"], "runtime_selected_frames": runtime["raw_storage_bytes"]//196608,
              "conversion": "native pixels; symmetric padding only; no smoothing; source checkout unchanged",
              "reference_art_publication_authorized": False}
    public_report.parent.mkdir(parents=True, exist_ok=True)
    public_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"animations": len(clips), "selected_frames": report["runtime_selected_frames"],
                      "idle_size": list(idle.size), "idle_bbox": idle.getchannel("A").getbbox(), "foot": foot_samples,
                      "raw_bytes": runtime["raw_storage_bytes"]}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    analyze(args.source.resolve(), args.output, args.report)
