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
    python tools/package_v4_sd.py --source <dir> --out <dir> [--json]
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
PACK_W, PACK_H = 256, 256
PACK_HEADER = 16
PACK_RECORD = 108
PACK_MAX_CLIPS = 6
PACK_MAX_FRAMES = 32
PACK_FRAME_BYTES = PACK_W * PACK_H * 3  # RGB565 + A8

SCENE_FILES = [f"scene-{i}.lwr" for i in range(THEMES)] + [
    f"scene-{i}-thumb.lwr" for i in range(THEMES)
]
CHARACTER_FILES = ["male.lwp", "qianqian-wave.lwp"]
REQUIRED = SCENE_FILES + CHARACTER_FILES
DIGEST_NAME = "littleworld-v4.sha256"

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPO_ROOT / "assets" / "visual-v4" / "runtime"
DEFAULT_OUT = REPO_ROOT / "dist" / "littleworld-v4-sd"


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
    """Mirror lw_pack_open(): magic, canvas, clip count, per-clip frames, size."""
    problems: list[str] = []
    info: dict = {"clips": 0, "frames": 0}
    data = path.read_bytes()
    if len(data) < PACK_HEADER:
        return [f"{path.name}: 文件过短（{len(data)} 字节）"], info
    if data[:8] != PACK_MAGIC:
        return [f"{path.name}: 魔数错误（应为 LWPX0001，实际 {data[:8]!r}）"], info

    w, h = struct.unpack_from("<HH", data, 8)
    clips = struct.unpack_from("<H", data, 12)[0]
    if (w, h) != (PACK_W, PACK_H):
        problems.append(f"{path.name}: 画布 {w}x{h}，应为 {PACK_W}x{PACK_H}")
    if clips == 0 or clips > PACK_MAX_CLIPS:
        return problems + [f"{path.name}: clip 数 {clips}，应在 1..{PACK_MAX_CLIPS}"], info

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
        # Byte 32.. of the record: u16 frames, u16 ?, u16 anchor_x, u16 anchor_y,
        # u16 dur, ... (see lw_pack.c). Only frame count matters for sizing.
        frames = struct.unpack_from("<H", data, off + 32)[0]
        if frames == 0 or frames > PACK_MAX_FRAMES:
            problems.append(
                f"{path.name}: clip {name!r} 帧数 {frames}，应在 1..{PACK_MAX_FRAMES}"
            )
        total_frames += frames

    expected = PACK_HEADER + PACK_RECORD * clips + total_frames * PACK_FRAME_BYTES
    if len(data) != expected:
        problems.append(
            f"{path.name}: 文件大小 {len(data)} 字节，按 {clips} clip / {total_frames} 帧"
            f" 应为 {expected}（可能截断或帧数据损坏）"
        )
    info = {"clips": clips, "frames": total_frames, "names": names}
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
def build_manifest(source: Path, digests: dict[str, str]) -> bytes:
    """Firmware-compatible manifest: tiny, ASCII, schema/version first."""
    doc = {
        "schema": SCHEMA,
        "version": PACK_VERSION,
        "pack": "littleworld-v4",
        "themes": 3,
        "characters": 2,
        "payloadBytes": sum(
            (source / name).stat().st_size for name in REQUIRED
        ),
    }
    text = json.dumps(doc, ensure_ascii=True, separators=(",", ":"), indent=2)
    raw = (text + "\n").encode("utf-8")
    if len(raw) > MANIFEST_MAX_BYTES:  # pragma: no cover - guard for future edits
        raise Problem(
            f"生成的清单 {len(raw)} 字节超过 {MANIFEST_MAX_BYTES}，固件无法完整读取"
        )
    del digests  # digests travel in the side-car file, not inside the manifest
    return raw


def build_digest_file(entries: dict[str, str]) -> bytes:
    lines = [f"{entries[name]}  {name}" for name in REQUIRED if name in entries]
    return ("\n".join(lines) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------
# Package / verify
# ---------------------------------------------------------------------------
def validate_source(source: Path) -> tuple[list[str], dict[str, dict]]:
    problems: list[str] = []
    details: dict[str, dict] = {}
    if not source.is_dir():
        return [f"资源目录不存在: {source}"], details

    for name in REQUIRED:
        if not (source / name).is_file():
            problems.append(f"缺少资源文件: {name}")

    for i in range(THEMES):
        scene = source / f"scene-{i}.lwr"
        thumb = source / f"scene-{i}-thumb.lwr"
        if scene.is_file():
            p = check_scene(scene, SCENE_W, SCENE_H)
            problems += p
            details[scene.name] = {"bytes": scene.stat().st_size, "kind": "scene"}
        if thumb.is_file():
            p = check_scene(thumb, THUMB_W, THUMB_H)
            problems += p
            details[thumb.name] = {"bytes": thumb.stat().st_size, "kind": "thumb"}

    for name in CHARACTER_FILES:
        path = source / name
        if not path.is_file():
            continue
        p, info = check_lwp(path)
        problems += p
        details[name] = {"bytes": path.stat().st_size, "kind": "character", **info}

    # 主题编号：只允许 scene-0/1/2，且不能多出 scene-3 之类
    problems += check_theme_numbering(source)
    return problems, details


def package(source: Path, out: Path) -> dict:
    problems, details = validate_source(source)
    if problems:
        raise Problem("\n".join(problems))

    root = out / "littleworld"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    for name in REQUIRED:
        shutil.copy2(source / name, root / name)

    digests = {name: sha256_file(root / name) for name in REQUIRED}
    manifest = build_manifest(source, digests)
    (root / MANIFEST_NAME).write_bytes(manifest)
    digests[MANIFEST_NAME] = sha256_file(root / MANIFEST_NAME)
    (root / DIGEST_NAME).write_bytes(build_digest_file(digests))

    # 清单本身也要过一次校验，确保固件读得懂
    mp, _ = check_manifest(root / MANIFEST_NAME)
    if mp:
        raise Problem("\n".join(mp))

    manifest_problems, _ = validate_source(source)
    report = {
        "generatedAt": _dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "gitCommit": _git_commit(),
        "schema": SCHEMA,
        "packVersion": PACK_VERSION,
        "source": str(source),
        "target": str(root),
        "manifestBytes": len(manifest),
        "totalBytes": sum((root / n).stat().st_size for n in digests),
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
    (out / "README.txt").write_text(_readme_text(), encoding="utf-8")
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

    for i in range(THEMES):
        s = root / f"scene-{i}.lwr"
        t = root / f"scene-{i}-thumb.lwr"
        if s.is_file():
            problems += check_scene(s, SCENE_W, SCENE_H)
        if t.is_file():
            problems += check_scene(t, THUMB_W, THUMB_H)

    for name in CHARACTER_FILES:
        p = root / name
        if p.is_file():
            probs, _info = check_lwp(p)
            problems += probs

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


def _readme_text() -> str:
    return (
        "Little World V4.1 TF 卡资源包\n"
        "================================\n\n"
        "把本目录下的 littleworld 文件夹整个复制到 TF 卡根目录，\n"
        "插回设备后开机会自动校验并启用新的场景与角色动画。\n\n"
        "目录结构（不要改名、不要改层级）：\n"
        "  littleworld/littleworld-v4.json\n"
        "  littleworld/scene-0.lwr  scene-0-thumb.lwr\n"
        "  littleworld/scene-1.lwr  scene-1-thumb.lwr\n"
        "  littleworld/scene-2.lwr  scene-2-thumb.lwr\n"
        "  littleworld/male.lwp\n"
        "  littleworld/qianqian-wave.lwp\n"
        "  littleworld/littleworld-v4.sha256   （校验清单，可留在卡上）\n\n"
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
    ap.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help="资源源目录")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="输出目录")
    ap.add_argument("--target", type=Path, help="校验目标（默认与 --out 相同）")
    ap.add_argument("--verify-only", action="store_true", help="只校验，不写文件")
    ap.add_argument("--raw-source", action="store_true", help="校验原始资源目录（不要求清单）")
    ap.add_argument("--json", action="store_true", help="输出机器可读 JSON")
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

        report = package(args.source, args.out)
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
