"""Little World V4.1 — TF card resource package builder / verifier.

The firmware reads a very small line-oriented manifest (see
firmware/components/lw_storage/src/lw_resource.c) and checks scene headers at
boot. This tool produces exactly that layout and validates every byte against
the same rules the firmware enforces:

    littleworld/littleworld-v4.json   schema=1, version=1..4, < 1024 bytes
    littleworld/scene-<n>.lwr         LWSR0001 1024x600, 16 + w*h*2 bytes
    littleworld/scene-<n>-thumb.lwr   LWSR0001 256x150,  16 + w*h*2 bytes
    littleworld/male.lwp              LWPX0001 256x256, clips<=6, frames<=32
    littleworld/qianqian-wave.lwp     LWPX0001 256x256, clips<=6, frames<=32

The firmware ignores extra files, so a `littleworld-v4.sha256` digest list can
travel on the card. A machine-readable PACKAGE.json is written next to the
`littleworld/` folder (never inside it) so the card stays minimal.

Stdlib only: this must run inside the ESP-IDF python env without pip installs.

Usage:
    python tools/package_v4_sd.py                      # build dist/littleworld-v4-sd
    python tools/package_v4_sd.py --verify-only        # verify the built package
    python tools/package_v4_sd.py --verify-only --target <dir>
    python tools/package_v4_sd.py --source <dir> [--source <dir2>] --out <dir> [--json]
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from typing import Sequence

# ---------------------------------------------------------------------------
# Contract constants — keep in sync with firmware/lw_storage/include/lw_resource.h
# ---------------------------------------------------------------------------
SCHEMA = 1
PACK_VERSION = 4
THEMES = 3
MANIFEST_NAME = "littleworld-v4.json"
MANIFEST_MAX_BYTES = 1023  # firmware reads sizeof(block)-1 == 1023 bytes

SCENE_MAGIC = b"LWSR0001"
SCENE_W, SCENE_H = 1024, 600
THUMB_W, THUMB_H = 256, 150

PACK_MAGIC = b"LWPX0001"
# The canvas size is NOT fixed: characters ship 256x256, the daily/dogbed pack
# 320x320, the component sheet 544x352 and the skin atlas 512x544. The firmware
# reads the geometry from the file header and bounds-checks it (lw_pack.c:
# width<=1024, height<=600, frame<=1,250,000), so the packager has to use the
# same rule or it rejects three of the four real packs.
PACK_MAX_W, PACK_MAX_H = 1024, 600
PACK_MAX_FRAME_BYTES = 1250000
PACK_HEADER = 16
PACK_RECORD = 108
PACK_MAX_CLIPS = 32
PACK_MAX_FRAMES = 32

# Optional geometry trailer (lw_pack.h). tools/lwp_prescale.py appends it to the
# packs it bakes: each clip records the alpha bounding box the frames were
# cropped to and a "prescaled" flag, so the runtime can drop lv_image scaling
# and take LVGL's non-transformed RGB565A8 blend path. A baked pack is therefore
# legitimately larger than the raw frame bytes, and this verifier has to know
# that or it rejects every baked card.
PACK_TRAILER_MAGIC = b"LWTR0001"
PACK_TRAILER_NONE = 0
PACK_TRAILER_GEOMETRY = 1
PACK_TRAILER_HEADER = 12
PACK_TRAILER_RECORD = 10

SCENE_FILES = [f"scene-{i}.lwr" for i in range(THEMES)] + [
    f"scene-{i}-thumb.lwr" for i in range(THEMES)
]
# Two characters. qianqian arrives as a wave pack for every checkout; her 13-clip
# base pack is private (see PRIVATE_FILES) and only lands in her one actor slot
# when the private build is used.
CHARACTER_FILES = ["male.lwp", "qianqian-wave.lwp"]
# Optional visual packs: absent on an older card, but validated when present.
VISUAL_FILES = ["daily.lwp", "ui.lwp", "skin.lwp"]
# Auxiliary room scenes for the pet/daily and RPG pages (one full slot, so the
# firmware loads exactly one of them at a time).
AUX_SCENE_FILES = [
    "daily-room.lwr",
    "daily-room-day.lwr",
    "daily-room-night.lwr",
    "rpg-room.lwr",
]
REQUIRED = SCENE_FILES + CHARACTER_FILES
OPTIONAL = VISUAL_FILES + AUX_SCENE_FILES
# qianqian-base.lwp (22,218,124 B, 13 clips) is a PRIVATE source pack: Codex
# froze it locally and explicitly asked that it is not published from a public
# checkout. Without it qianqian still renders — the firmware falls back to its
# embedded LWFA idle frame and then appends qianqian-wave.lwp — but she loses
# sleep/walk. `--private` ships it and writes to a separate, gitignored output.
PRIVATE_FILES = ["qianqian-base.lwp"]
# Optional text content (memories / album / letters / dialogues). It is built by
# tools/package_v4_content.py from content/*.lwcx, but the packer copies it in
# as well so that re-running the packer can never silently drop the card's text.
CONTENT_NAME = "content.lwcx"
CONTENT_MAGIC = "LWCX0001"
DIGEST_NAME = "littleworld-v4.sha256"

REPO_ROOT = Path(__file__).resolve().parents[1]
# The real V4.3 assets are split across three source directories: the frozen
# characters live in visual-v4-2, the skin atlas in visual-v4-3, and the older
# scenes/thumbnails in visual-v4. A single hard-coded source is what made the
# old packager ship the 3-clip 5 MB male.lwp instead of the frozen 7-clip
# 17.9 MB one, so every file is looked up across all of them, in this order.
DEFAULT_SOURCES = [
    REPO_ROOT / "assets" / "visual-v4-2" / "runtime",
    REPO_ROOT / "assets" / "visual-v4-3" / "runtime",
    REPO_ROOT / "assets" / "visual-v4" / "runtime",
]
DEFAULT_OUT = REPO_ROOT / "dist" / "littleworld-v4-sd"
# content.lwcx is produced by tools/package_v4_content.py, not by the art export,
# so it is resolved from the repo unless a source directory carries one.
CONTENT_SOURCE = REPO_ROOT / "content" / CONTENT_NAME


class Problem(Exception):
    pass


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------
def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_scene(path: Path, w: int, h: int) -> list[str]:
    """Mirror scene_ok(): magic + u16 LE width/height, plus exact payload size."""
    problems: list[str] = []
    data = path.read_bytes()
    if len(data) < 16:
        return [f"{path.name}: 文件过短（{len(data)} 字节），连 16 字节头都不足"]
    if data[:8] != SCENE_MAGIC:
        problems.append(
            f"{path.name}: 魔数错误（应为 LWSR0001，实际 {data[:8]!r}）"
        )
        return problems
    gw, gh = struct.unpack_from("<HH", data, 8)
    if (gw, gh) != (w, h):
        problems.append(f"{path.name}: 尺寸 {gw}x{gh}，应为 {w}x{h}")
    expected = 16 + w * h * 2
    if len(data) != expected:
        problems.append(
            f"{path.name}: 文件大小 {len(data)} 字节，按 {w}x{h} RGB565 应为 {expected}"
        )
    return problems


def check_lwp(path: Path) -> tuple[list[str], dict]:
    """Mirror lw_pack_open(): magic, per-file geometry, clip count, frames, size."""
    problems: list[str] = []
    info: dict = {"clips": 0, "frames": 0}
    data = path.read_bytes()
    if len(data) < PACK_HEADER:
        return [f"{path.name}: 文件过短（{len(data)} 字节）"], info
    if data[:8] != PACK_MAGIC:
        return [f"{path.name}: 魔数错误（应为 LWPX0001，实际 {data[:8]!r}）"], info

    w, h = struct.unpack_from("<HH", data, 8)
    clips = struct.unpack_from("<H", data, 12)[0]
    if w == 0 or h == 0 or w > PACK_MAX_W or h > PACK_MAX_H:
        return problems + [
            f"{path.name}: 画布 {w}x{h}，应在 1x1..{PACK_MAX_W}x{PACK_MAX_H}"
        ], info
    frame_bytes = w * h * 3  # RGB565 + A8
    if frame_bytes > PACK_MAX_FRAME_BYTES:
        return problems + [
            f"{path.name}: 单帧 {frame_bytes} 字节，超过固件上限 {PACK_MAX_FRAME_BYTES}"
        ], info
    if clips == 0 or clips > PACK_MAX_CLIPS:
        return problems + [f"{path.name}: clip 数 {clips}，应在 1..{PACK_MAX_CLIPS}"], info

    # Geometry trailer, mirroring the block in lw_pack.c. The trailer sits at the
    # tail of the file, so every clip offset is bounded by payload_end rather
    # than by the file length.
    kind = struct.unpack_from("<H", data, 14)[0]
    trailer_bytes = 0
    payload_end = len(data)
    if kind == PACK_TRAILER_GEOMETRY:
        trailer_bytes = PACK_TRAILER_HEADER + PACK_TRAILER_RECORD * clips
        payload_start = PACK_HEADER + PACK_RECORD * clips
        if len(data) < payload_start + trailer_bytes:
            return problems + [f"{path.name}: 几何 trailer 被截断"], info
        start = len(data) - trailer_bytes
        if (data[start : start + 8] != PACK_TRAILER_MAGIC
                or struct.unpack_from("<H", data, start + 8)[0] != clips):
            return problems + [f"{path.name}: 几何 trailer 头错误（魔数或 clip 数）"], info
        for i in range(clips):
            rec = start + PACK_TRAILER_HEADER + i * PACK_TRAILER_RECORD
            bx, by, bw, bh, prescaled = struct.unpack_from("<HHHHH", data, rec)
            if prescaled > PACK_TRAILER_GEOMETRY:
                problems.append(f"{path.name}: clip {i} 的 prescaled={prescaled} 非法")
            if bw and bh and (bx + bw > w or by + bh > h):
                problems.append(
                    f"{path.name}: clip {i} 的 bbox ({bx},{by},{bw},{bh}) 超出 {w}x{h}"
                )
        payload_end = start
    elif kind != PACK_TRAILER_NONE:
        return problems + [f"{path.name}: 未知 trailer kind {kind}"], info

    total_frames = 0
    names: list[str] = []
    for i in range(clips):
        off = PACK_HEADER + i * PACK_RECORD
        if off + PACK_RECORD > len(data):
            problems.append(f"{path.name}: 第 {i} 个 clip 记录被截断")
            break
        raw_name = data[off : off + 32].split(b"\0", 1)[0]
        name = raw_name.decode("ascii", "replace")
        if not name:
            problems.append(f"{path.name}: 第 {i} 个 clip 名称为空")
        names.append(name)
        # Byte 32.. of the record: u16 frames, s16 anchor_x, s16 anchor_y,
        # u8 loop, u32 offset, 32 x u16 durations (see lw_pack.c). The offset
        # must be validated against the real frame stride, which is exactly the
        # check that was missing while the canvas was assumed to be 256x256.
        frames, anchor_x, anchor_y = struct.unpack_from("<Hhh", data, off + 32)
        offset = struct.unpack_from("<I", data, off + 40)[0]
        if frames == 0 or frames > PACK_MAX_FRAMES:
            problems.append(
                f"{path.name}: clip {name!r} 帧数 {frames}，应在 1..{PACK_MAX_FRAMES}"
            )
        if anchor_x < 0 or anchor_y < 0 or anchor_x > w or anchor_y > h:
            problems.append(
                f"{path.name}: clip {name!r} 锚点 ({anchor_x},{anchor_y}) 超出 {w}x{h}"
            )
        if offset < PACK_HEADER + PACK_RECORD * clips:
            problems.append(f"{path.name}: clip {name!r} 数据偏移 {offset} 落入记录区")
        elif offset + frames * frame_bytes > payload_end:
            problems.append(
                f"{path.name}: clip {name!r} 数据越界（{offset}+{frames}x{frame_bytes}）"
            )
        total_frames += frames

    expected = (PACK_HEADER + PACK_RECORD * clips + total_frames * frame_bytes
                + trailer_bytes)
    if len(data) != expected:
        problems.append(
            f"{path.name}: 文件大小 {len(data)} 字节，按 {clips} clip / {total_frames} 帧"
            f" / {w}x{h} 应为 {expected}（可能截断或帧数据损坏）"
        )
    info = {"clips": clips, "frames": total_frames, "names": names, "width": w, "height": h}
    return problems, info


def check_theme_numbering(root: Path) -> list[str]:
    """固件只认 scene-0..2；多出来的编号（或认不出的名字）必须报出来。"""
    problems: list[str] = []
    for extra in sorted(root.glob("scene-*.lwr")):
        m = re.fullmatch(r"scene-(\d+)(-thumb)?\.lwr", extra.name)
        if not m:
            problems.append(f"无法识别的场景文件名: {extra.name}")
            continue
        if int(m.group(1)) >= THEMES:
            problems.append(
                f"主题编号越界: {extra.name}（固件只认 0..{THEMES - 1}）"
            )
    return problems

def check_manifest(path: Path) -> tuple[list[str], dict]:
    problems: list[str] = []
    data = path.read_bytes()
    if len(data) > MANIFEST_MAX_BYTES:
        return [
            f"{MANIFEST_NAME}: 清单 {len(data)} 字节，超过固件一次读取的 "
            f"{MANIFEST_MAX_BYTES} 字节上限（固件会截断解析）"
        ], {}
    try:
        obj = json.loads(data.decode("utf-8"))
    except Exception as exc:  # noqa: BLE001 - report, don't crash
        return [f"{MANIFEST_NAME}: 不是合法 JSON：{exc}"], {}

    schema = obj.get("schema")
    version = obj.get("version")
    if not isinstance(schema, int):
        problems.append(f"{MANIFEST_NAME}: 缺少整数 schema")
    elif schema != SCHEMA:
        problems.append(f"{MANIFEST_NAME}: schema={schema}，固件要求 {SCHEMA}")
    if not isinstance(version, int):
        problems.append(f"{MANIFEST_NAME}: 缺少整数 version")
    elif version < 1 or version > PACK_VERSION:
        problems.append(
            f"{MANIFEST_NAME}: version={version}，固件接受 1..{PACK_VERSION}"
        )

    # 固件用 strstr 找键，再 strtoul 取数字：确认这段文本真的能被它读懂
    text = data.decode("utf-8", "replace")
    for key in ("schema", "version"):
        m = re.search(rf'"{key}"\s*:\s*(-?\d+)', text)
        if not m:
            problems.append(f"{MANIFEST_NAME}: 固件的行扫描找不到 {key} 数值")
    return problems, obj


# ---------------------------------------------------------------------------
# Manifest / digest generation
# ---------------------------------------------------------------------------
def build_manifest(root: Path, shipped: Sequence[str]) -> bytes:
    """Firmware-compatible manifest: tiny, ASCII, schema/version first.

    payloadBytes must describe what is actually on the card, so it is summed
    over the shipped payload files (never over a fixed list: optional packs are
    absent on older cards and the firmware compares this against the bytes it
    can actually see).
    """
    payload = sum((root / name).stat().st_size for name in shipped)
    doc = {
        "schema": SCHEMA,
        "version": PACK_VERSION,
        "pack": "littleworld-v4",
        "themes": 3,
        "characters": 2,
        "payloadBytes": payload,
    }
    text = json.dumps(doc, ensure_ascii=True, separators=(",", ":"), indent=2)
    raw = (text + "\n").encode("utf-8")
    if len(raw) > MANIFEST_MAX_BYTES:  # pragma: no cover - guard for future edits
        raise Problem(
            f"生成的清单 {len(raw)} 字节超过 {MANIFEST_MAX_BYTES}，固件无法完整读取"
        )
    return raw


def build_digest_file(entries: dict[str, str], shipped: Sequence[str]) -> bytes:
    lines = [f"{entries[name]}  {name}" for name in shipped if name in entries]
    return ("\n".join(lines) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------
# Package / verify
# ---------------------------------------------------------------------------
def resolve_source(name: str, sources: Sequence[Path]) -> Path | None:
    """Return the first source directory that actually contains `name`."""
    for source in sources:
        candidate = source / name
        if candidate.is_file():
            return candidate
    return None


def collect_files(
    sources: Sequence[Path], include_private: bool = False
) -> tuple[dict[str, Path], list[str], list[str]]:
    """Resolve every required/optional file across the source directories."""
    found: dict[str, Path] = {}
    wanted = REQUIRED + OPTIONAL + (PRIVATE_FILES if include_private else [])
    for name in wanted:
        hit = resolve_source(name, sources)
        if hit is not None:
            found[name] = hit
    missing_required = [n for n in REQUIRED if n not in found]
    missing_optional = [n for n in (OPTIONAL + PRIVATE_FILES) if n not in found]
    return found, missing_required, missing_optional


def _check_by_kind(path: Path) -> tuple[list[str], dict]:
    """Pick the right validator from the file name."""
    if path.suffix == ".lwr":
        w, h = (SCENE_W, SCENE_H) if "-thumb" not in path.name else (THUMB_W, THUMB_H)
        return check_scene(path, w, h), {"bytes": path.stat().st_size, "kind": "scene"}
    problems, info = check_lwp(path)
    if path.name in CHARACTER_FILES or path.name in PRIVATE_FILES:
        kind = "character"
    else:
        kind = "visual"
    return problems, {"bytes": path.stat().st_size, "kind": kind, **info}


def validate_source(
    sources: Sequence[Path], include_private: bool = False
) -> tuple[list[str], dict[str, dict]]:
    problems: list[str] = []
    details: dict[str, dict] = {}
    sources = [s for s in sources]
    if not sources:
        return ["没有提供资源目录"], details
    existing = [s for s in sources if s.is_dir()]
    if not existing:
        return [f"资源目录不存在: {', '.join(str(s) for s in sources)}"], details

    found, missing_required, _missing_optional = collect_files(sources, include_private)
    for name in missing_required:
        problems.append(f"缺少资源文件: {name}")

    for name, path in found.items():
        p, meta = _check_by_kind(path)
        problems += p
        details[name] = {**meta, "source": str(path.parent)}

    # 主题编号：只允许 scene-0/1/2，且不能多出 scene-3 之类
    for source in existing:
        problems += check_theme_numbering(source)
    return problems, details


def package(sources: Sequence[Path], out: Path, include_private: bool = False) -> dict:
    problems, details = validate_source(sources, include_private)
    if problems:
        raise Problem("\n".join(problems))
    found, _missing_required, missing_optional = collect_files(sources, include_private)
    # content.lwcx is produced by tools/package_v4_content.py, not by the art
    # export, so it is resolved from the repo unless a source dir carries one.
    if CONTENT_NAME not in found and CONTENT_SOURCE.is_file():
        found[CONTENT_NAME] = CONTENT_SOURCE

    root = out / "littleworld"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    shipped: list[str] = []
    order = REQUIRED + OPTIONAL + PRIVATE_FILES + [CONTENT_NAME]
    for name in order:
        if name in found:
            shutil.copy2(found[name], root / name)
            shipped.append(name)

    digests = {name: sha256_file(root / name) for name in shipped}
    manifest = build_manifest(root, shipped)
    (root / MANIFEST_NAME).write_bytes(manifest)
    digests[MANIFEST_NAME] = sha256_file(root / MANIFEST_NAME)
    (root / DIGEST_NAME).write_bytes(build_digest_file(digests, shipped))

    # 清单本身也要过一次校验，确保固件读得懂
    mp, _ = check_manifest(root / MANIFEST_NAME)
    if mp:
        raise Problem("\n".join(mp))

    manifest_problems, _ = validate_source([root], include_private=True)
    report = {
        "generatedAt": _dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "gitCommit": _git_commit(),
        "schema": SCHEMA,
        "packVersion": PACK_VERSION,
        "sources": [str(s) for s in sources],
        "target": str(root),
        "private": include_private,
        "manifestBytes": len(manifest),
        "totalBytes": sum((root / n).stat().st_size for n in digests),
        "missingOptional": missing_optional,
        "files": {
            name: {
                "bytes": (root / name).stat().st_size,
                "sha256": digests[name],
                **details.get(name, {}),
            }
            for name in digests
        },
        "validation": {"problems": manifest_problems, "ok": not manifest_problems},
    }
    (out / "PACKAGE.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (out / "README.txt").write_text(_readme_text(include_private), encoding="utf-8")
    return report


def verify(target: Path, require_manifest: bool = True) -> tuple[list[str], dict]:
    """Verify a built package (dist/.../littleworld) or a raw runtime folder."""
    root = target
    if (target / "littleworld").is_dir():
        root = target / "littleworld"
    problems: list[str] = []

    wanted = list(REQUIRED) + ([MANIFEST_NAME] if require_manifest else [])
    for name in wanted:
        if not (root / name).is_file():
            problems.append(f"缺少文件: {name}")
    if not (root / MANIFEST_NAME).is_file():
        # 原始资源目录本来就没有清单（打包时生成），不算错误
        return problems, {"target": str(root), "manifestMissing": require_manifest}

    mp, mobj = check_manifest(root / MANIFEST_NAME)
    problems += mp

    for name in REQUIRED + OPTIONAL + PRIVATE_FILES:
        path = root / name
        if not path.is_file():
            continue
        if path.suffix == ".lwr":
            w, h = (SCENE_W, SCENE_H) if "-thumb" not in name else (THUMB_W, THUMB_H)
            problems += check_scene(path, w, h)
            continue
        probs, _info = check_lwp(path)
        problems += probs

    content = root / CONTENT_NAME
    if content.is_file():
        # content.lwcx is a *text* container: the magic is a line after the
        # leading '#' comments, not the first eight bytes of the file.
        text = content.read_text(encoding="utf-8", errors="replace")
        if CONTENT_MAGIC not in text.splitlines():
            problems.append(f"{CONTENT_NAME}: 缺少 {CONTENT_MAGIC} 行")

    problems += check_theme_numbering(root)

    # 哈希清单：存在就逐条核对
    digest_path = root / DIGEST_NAME
    if digest_path.is_file():
        for line in digest_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line:
                continue
            parts = line.split(None, 1)
            if len(parts) != 2:
                problems.append(f"{DIGEST_NAME}: 无法解析的行 {line!r}")
                continue
            want, name = parts[0].lower(), parts[1].strip()
            f = root / name
            if not f.is_file():
                problems.append(f"{DIGEST_NAME}: 引用了不存在的文件 {name}")
                continue
            got = sha256_file(f)
            if got != want:
                problems.append(f"{name}: SHA-256 不一致（清单 {want[:12]}…，实际 {got[:12]}…）")

    return problems, {
        "target": str(root),
        "manifest": mobj,
        "files": sorted(p.name for p in root.iterdir() if p.is_file()),
    }


def _git_commit() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        return out.stdout.strip() or "unknown"
    except Exception:  # noqa: BLE001 - best effort only
        return "unknown"


def _readme_text(include_private: bool = False) -> str:
    base = (
        "Little World V4.3 TF 卡资源包\n"
        "================================\n\n"
        "把本目录下的 littleworld 文件夹整个复制到 TF 卡根目录，\n"
        "插回设备后开机会自动校验并启用新的场景与角色动画。\n"
        "最终卡上的路径必须是   /sdcard/littleworld/  （littleworld 并列于卡根，\n"
        "不要再套一层 littleworld，即不存在 /sdcard/littleworld/littleworld）。\n\n"
        "目录结构（不要改名、不要改层级）：\n"
        "  /sdcard/littleworld/littleworld-v4.json        （清单，固件先读它）\n"
        "  /sdcard/littleworld/scene-0.lwr  scene-0-thumb.lwr\n"
        "  /sdcard/littleworld/scene-1.lwr  scene-1-thumb.lwr\n"
        "  /sdcard/littleworld/scene-2.lwr  scene-2-thumb.lwr\n"
        "  /sdcard/littleworld/male.lwp              （男方 7 段动画）\n"
    )
    if include_private:
        qianqian = (
            "  /sdcard/littleworld/qianqian-base.lwp     （千千 13 段动画，私有素材）\n"
        )
    else:
        qianqian = (
            "  qianqian-base.lwp 不在本包内：它含私有素材，只打进私有包。\n"
            "  没有它时千千用内置待机帧 + 招手包，画面正常但动作少。\n"
        )
    return (
        base
        + qianqian
        + "  /sdcard/littleworld/qianqian-wave.lwp     （千千招手）\n"
        "  以下为可选，缺了不会崩，只是少一层美术：\n"
        "  /sdcard/littleworld/daily.lwp             （双人坐姿与狗窝 8 段）\n"
        "  /sdcard/littleworld/ui.lwp                （Meowa 组件图集）\n"
        "  /sdcard/littleworld/skin.lwp              （17 种皮肤的图集）\n"
        "  /sdcard/littleworld/daily-room.lwr  daily-room-day.lwr\n"
        "  /sdcard/littleworld/daily-room-night.lwr  rpg-room.lwr\n"
        "  /sdcard/littleworld/littleworld-v4.sha256  （校验清单，可留在卡上）\n\n"
        "说明：\n"
        "1. 如果卡上已经存在 littleworld 文件夹，复制时会提示是否覆盖；\n"
        "   建议先备份旧文件夹再覆盖。\n"
        "2. 资源缺失或损坏时，设备会自动回退到内置画面，不会死机。\n"
        "3. 校验方法：在电脑上运行 tools/verify_v4_sd.ps1 -Target <卡路径>。\n"
        "4. 不要修改 littleworld-v4.json 里的 schema / version 数字。\n"
    )


def _force_utf8_stdout() -> None:
    """Windows 控制台默认 GBK，中文会变成乱码；能改就改成 UTF-8。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
        except Exception:  # noqa: BLE001 - old python or redirected stream
            pass


def main(argv: list[str] | None = None) -> int:
    _force_utf8_stdout()
    ap = argparse.ArgumentParser(description="Little World V4 TF 卡资源打包/校验")
    ap.add_argument(
        "--source",
        type=Path,
        action="append",
        help="资源源目录（可重复；默认按 visual-v4-2 → visual-v4-3 → visual-v4 顺序查找）",
    )
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="输出目录")
    ap.add_argument("--target", type=Path, help="校验目标（默认与 --out 相同）")
    ap.add_argument("--verify-only", action="store_true", help="只校验，不写文件")
    ap.add_argument("--raw-source", action="store_true", help="校验原始资源目录（不要求清单）")
    ap.add_argument("--json", action="store_true", help="输出机器可读 JSON")
    ap.add_argument(
        "--private",
        action="store_true",
        help="把私有的 qianqian-base.lwp 也打进去（只用于本地卡，禁止提交/发布）",
    )
    args = ap.parse_args(argv)

    try:
        if args.verify_only:
            target = args.target or args.out
            problems, info = verify(target, require_manifest=not args.raw_source)
            result = {"ok": not problems, "problems": problems, **info}
            if args.json:
                print(json.dumps(result, ensure_ascii=False, indent=2))
            else:
                if problems:
                    print(f"[FAIL] 发现 {len(problems)} 个问题：")
                    for p in problems:
                        print("  - " + p)
                else:
                    print(f"[OK] 校验通过：{info.get('target')}")
                    files = info.get("files", [])
                    print(f"      文件 {len(files)} 个，清单与哈希全部匹配")
            return 0 if not problems else 2

        report = package(args.source or DEFAULT_SOURCES, args.out, args.private)
        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print("[OK] 资源包已生成")
            print(f"  输出目录: {report['target']}")
            print(f"  文件数:   {len(report['files'])}（含清单与哈希清单）")
            print(f"  总大小:   {report['totalBytes']:,} 字节")
            print(f"  清单大小: {report['manifestBytes']} 字节（固件上限 1023）")
            for name, meta in report["files"].items():
                print(f"   - {name:<28} {meta['bytes']:>9,}  {meta['sha256'][:16]}…")
            if report["missingOptional"]:
                print(
                    "  提示：可选资源未找到（不影响启动）："
                    + ", ".join(report["missingOptional"])
                )
        return 0
    except Problem as exc:
        print("[FAIL] 资源不满足固件要求：", file=sys.stderr)
        for line in str(exc).splitlines():
            print("  - " + line, file=sys.stderr)
        return 2
    except Exception as exc:  # noqa: BLE001 - surface, never hide
        print(f"[ERROR] {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
