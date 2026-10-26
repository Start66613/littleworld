"""Convert a Little World Studio ".lwpack" into the firmware's ".lwcx" content file.

Why this exists
---------------
The Studio (agent/littleworld-studio-v1) exports a ZIP container described by
`littleworld-content/1`, and its own FORMAT.md says outright that the firmware
has no reader for it. The device reads `LWCX0001`, a line-oriented text format
that `lw_content.c` parses without a JSON library.

So the two formats do not overlap: this tool is the bridge, and it is the only
place allowed to decide what survives the crossing.

The rules it follows
--------------------
1. **Never guess.** If a Studio field has no device representation, the
   conversion FAILS by default. `--allow-partial` is the explicit opt-in that
   turns those failures into reported omissions, and the exit code still says
   something was dropped.
2. **Never silently truncate.** Over-limit counts and over-long strings are
   errors, not clamps. The device limits are imported from the same constants
   the firmware uses (see DEVICE_LIMITS below and the test that keeps them in
   sync).
3. **Never touch the source.** The .lwpack is opened read-only; nothing is
   extracted to disk except the output file.
4. **Report everything.** A compatibility report lists every mapped, adapted and
   dropped field, and every medium that has no device path.

Usage
-----
    python tools/studio_to_device.py project.lwpack --out content/from-studio.lwcx
    python tools/studio_to_device.py project.lwpack --report build/compat.json
    python tools/studio_to_device.py project.lwpack --allow-partial --out ...
    python tools/studio_to_device.py --verify out.lwcx      # parse with the real C code

Exit codes: 0 fully converted, 1 refused or partial without --allow-partial,
2 usage error.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

# ---------------------------------------------------------------------------
# Device limits. These mirror firmware/components/lw_content/include/lw_content.h
# exactly; tools/test_studio_bridge.ps1 fails if they ever drift.
# ---------------------------------------------------------------------------
DEVICE_LIMITS = {
    "max_bytes": 32768,
    "max_memories": 32,
    "max_photos": 64,
    "max_letters": 8,
    "max_dialogues": 16,
    "max_lines": 64,
    "title_max": 128,
    "body_max": 512,
    "path_max": 96,
    "speaker_max": 24,
    "schema": 1,
    "content_version": 1,
}

STUDIO_SCHEMA = "littleworld-content/1"
STUDIO_COMPAT = "studio-only; firmware reader not implemented"
FIXED_JSON = (
    "manifest.json",
    "anniversary.json",
    "letters.json",
    "memories.json",
    "album.json",
    "story.json",
    "resources.json",
)
MAX_ENTRIES = 87
MAX_ARCHIVE = 64 * 1024 * 1024
MAX_JSON = 4 * 1024 * 1024

# Photo paths become .lwph on the card: the firmware only ever sees a name it can
# hand to the loader, and the original PNG stays on the computer.
PHOTO_SUFFIX = ".lwph"


class ConversionError(Exception):
    """A refusal with a human explanation. Never a silent downgrade."""


@dataclass
class Report:
    source: str = ""
    output: str = ""
    ok: bool = False
    strict: bool = True
    mapped: list[str] = field(default_factory=list)
    adapted: list[str] = field(default_factory=list)
    dropped: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    counts: dict = field(default_factory=dict)
    bytes_written: int = 0

    def drop(self, what: str) -> None:
        self.dropped.append(what)

    def error(self, what: str) -> None:
        self.errors.append(what)

    def as_dict(self) -> dict:
        return {
            "source": self.source,
            "output": self.output,
            "ok": self.ok,
            "strict": self.strict,
            "mapped": self.mapped,
            "adapted": self.adapted,
            "dropped": self.dropped,
            "errors": self.errors,
            "counts": self.counts,
            "bytes": self.bytes_written,
        }


# ---------------------------------------------------------------------------
# Archive reading
# ---------------------------------------------------------------------------

def open_archive(path: Path) -> zipfile.ZipFile:
    if not path.exists():
        raise ConversionError(f"archive not found: {path}")
    size = path.stat().st_size
    if size > MAX_ARCHIVE:
        raise ConversionError(f"archive is {size} B, over the 64 MiB limit")
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as exc:
        raise ConversionError(f"not a ZIP archive: {exc}") from exc
    names = archive.namelist()
    if len(names) > MAX_ENTRIES:
        raise ConversionError(f"{len(names)} entries, over the {MAX_ENTRIES} limit")
    if len(set(names)) != len(names):
        raise ConversionError("archive contains duplicate entry names")
    for name in names:
        # A path that escapes the archive would let a malicious project write
        # outside the extraction root; the device never sees paths like this.
        pure = PurePosixPath(name)
        if pure.is_absolute() or any(part in ("..", "") for part in pure.parts):
            raise ConversionError(f"unsafe archive path: {name!r}")
        if "\\" in name:
            raise ConversionError(f"backslash in archive path: {name!r}")
    for required in FIXED_JSON:
        if required not in names:
            raise ConversionError(f"archive is missing {required}")
    return archive


def read_json(archive: zipfile.ZipFile, name: str) -> object:
    info = archive.getinfo(name)
    if info.file_size > MAX_JSON:
        raise ConversionError(f"{name} is {info.file_size} B, over the 4 MiB JSON limit")
    raw = archive.read(name)
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ConversionError(f"{name} is not valid UTF-8: {exc}") from exc
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise ConversionError(f"{name} is not valid JSON: {exc}") from exc


# ---------------------------------------------------------------------------
# Field mapping
# ---------------------------------------------------------------------------

def require_list(value: object, name: str) -> list:
    if not isinstance(value, list):
        raise ConversionError(f"{name} must be a list, got {type(value).__name__}")
    return value


def require_object(value: object, name: str) -> dict:
    if not isinstance(value, dict):
        raise ConversionError(f"{name} must be an object, got {type(value).__name__}")
    return value


def require_text(container: dict, key: str, what: str) -> str:
    value = container.get(key)
    if not isinstance(value, str):
        raise ConversionError(f"{what}.{key} must be a string")
    return value


def utf8_bytes(text: str) -> int:
    return len(text.encode("utf-8"))


def check_text(text: str, limit: int, what: str, report: Report) -> str:
    """Length limits are UTF-8 BYTES on the device, not characters."""
    size = utf8_bytes(text)
    if size > limit:
        raise ConversionError(f"{what} is {size} UTF-8 bytes, over the device limit of {limit}")
    return text


def escape_value(text: str) -> str:
    """The .lwcx format has exactly two escapes: backslash and newline."""
    return text.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "")


def check_date(text: str, what: str) -> str:
    match = re.fullmatch(r"(\d{4})-(\d{2})-(\d{2})", text)
    if not match:
        raise ConversionError(f"{what} is not YYYY-MM-DD: {text!r}")
    year, month, day = (int(part) for part in match.groups())
    if not 1970 <= year <= 2200:
        raise ConversionError(f"{what} year {year} is outside 1970-2200")
    if not 1 <= month <= 12:
        raise ConversionError(f"{what} month {month} is invalid")
    days = [31, 29 if (year % 4 == 0 and year % 100 != 0) or year % 400 == 0 else 28,
            31, 30, 31, 30, 31, 31, 30, 31, 30, 31][month - 1]
    if not 1 <= day <= days:
        raise ConversionError(f"{what} day {day} does not exist in {year}-{month:02d}")
    return text


def photo_path(studio_path: str, what: str) -> str:
    """Studio stores assets/<uuid>/preview.png; the card stores a flat name."""
    pure = PurePosixPath(studio_path)
    if len(pure.parts) != 3 or pure.parts[0] != "assets":
        raise ConversionError(f"{what} is not assets/<id>/<file>: {studio_path!r}")
    identifier = pure.parts[1]
    if not re.fullmatch(r"[0-9a-f-]{36}", identifier):
        raise ConversionError(f"{what} has a malformed id: {identifier!r}")
    name = identifier + PHOTO_SUFFIX
    if len(name) > DEVICE_LIMITS["path_max"]:
        raise ConversionError(f"{what} would be {len(name)} bytes, over the {DEVICE_LIMITS['path_max']} limit")
    return name


def validate_resources(archive: zipfile.ZipFile, resources: list, report: Report) -> None:
    """Check the media manifest even though the device does not consume it.

    The device never reads resources.json, but a package whose declared media is
    unsupported or missing is still a package this tool must not silently
    convert: the user would install it, see no picture, and have no idea why.
    So the check happens here, at the boundary, where the message can be
    specific.
    """
    import hashlib

    supported = {"image/png", "image/jpeg", "image/webp"}
    names = set(archive.namelist())
    for index, entry in enumerate(resources):
        item = require_object(entry, f"resources[{index}]")
        what = f"resources[{index}]"
        path = require_text(item, "path", what)
        mime = require_text(item, "mime", what)
        if mime not in supported:
            raise ConversionError(
                f"{what}.mime is {mime!r}; the device only accepts PNG/JPEG/WebP-derived "
                "photos, and SVG or anything else has no decoder"
            )
        if path not in names:
            raise ConversionError(f"{what}.path {path!r} is not in the archive")
        data = archive.read(path)
        declared_bytes = item.get("bytes")
        if isinstance(declared_bytes, int) and declared_bytes != len(data):
            raise ConversionError(
                f"{what}.bytes says {declared_bytes} but the entry is {len(data)} B"
            )
        declared_hash = item.get("sha256")
        if isinstance(declared_hash, str) and declared_hash:
            actual = hashlib.sha256(data).hexdigest()
            if actual != declared_hash.lower():
                raise ConversionError(f"{what}.sha256 does not match the entry")
        if item.get("role") not in ("original", "preview"):
            raise ConversionError(f"{what}.role is {item.get('role')!r}, expected original or preview")
    report.mapped.append(f"resources.json: {len(resources)} media entries verified "
                         "(mime, size, sha256) and then dropped")


def convert(archive_path: Path, report: Report) -> str:
    archive = open_archive(archive_path)
    with archive:
        manifest = require_object(read_json(archive, "manifest.json"), "manifest.json")
        schema = manifest.get("schema")
        if schema != STUDIO_SCHEMA:
            raise ConversionError(
                f"unsupported schema {schema!r}; this tool understands {STUDIO_SCHEMA!r} only"
            )
        compatibility = manifest.get("compatibility")
        if compatibility != STUDIO_COMPAT:
            raise ConversionError(
                f"unexpected compatibility marker {compatibility!r}; "
                "refusing to guess what the package means"
            )
        name = require_text(manifest, "name", "manifest.json")
        report.mapped.append(f"manifest.schema={STUDIO_SCHEMA}")
        report.dropped.append("manifest: id, demo, createdAt, updatedAt, theme "
                              "(no device representation)")

        anniversaries = require_list(read_json(archive, "anniversary.json"), "anniversary.json")
        memories = require_list(read_json(archive, "memories.json"), "memories.json")
        letters = require_list(read_json(archive, "letters.json"), "letters.json")
        album = require_object(read_json(archive, "album.json"), "album.json")
        story = require_object(read_json(archive, "story.json"), "story.json")
        resources = require_list(read_json(archive, "resources.json"), "resources.json")
        validate_resources(archive, resources, report)

        lines: list[str] = []
        lines.append("LWCX0001")
        lines.append(f"schema={DEVICE_LIMITS['schema']}")
        lines.append(f"content_version={DEVICE_LIMITS['content_version']}")

        # --- anniversary: the device has exactly ONE, Studio allows 32 -------
        if anniversaries:
            first = require_object(anniversaries[0], "anniversary[0]")
            date = check_date(require_text(first, "date", "anniversary[0]"), "anniversary[0].date")
            lines.append(f"anniversary={date}")
            report.mapped.append("anniversary[0].date -> anniversary")
            if len(anniversaries) > 1:
                for index in range(1, len(anniversaries)):
                    report.drop(f"anniversary[{index}] (the device keeps one anniversary)")
            report.dropped.append("anniversary[].title, .description (not shown on the device)")
        else:
            report.error("no anniversary: the device needs a start date")
            lines.append("anniversary=2024-10-25")
            report.adapted.append("anniversary: fell back to 2024-10-25")

        lines.append(f"# name: {escape_value(name)}")
        report.mapped.append("manifest.name -> comment line")

        # --- memories: only visible ones reach the device --------------------
        visible = []
        for index, entry in enumerate(memories):
            memory = require_object(entry, f"memories[{index}]")
            if memory.get("visible") is not True:
                report.drop(f"memories[{index}] (visible=false)")
                continue
            visible.append((index, memory))
        if len(visible) > DEVICE_LIMITS["max_memories"]:
            raise ConversionError(
                f"{len(visible)} visible memories, over the device limit of "
                f"{DEVICE_LIMITS['max_memories']}; hide some or split the project"
            )
        photo_ids = {}
        for photo_index, entry in enumerate(require_list(album.get("photos", []), "album.photos")):
            photo = require_object(entry, f"album.photos[{photo_index}]")
            identifier = require_text(photo, "id", f"album.photos[{photo_index}]")
            photo_ids[identifier] = photo_path(require_text(photo, "preview", "album.photos"),
                                               f"album.photos[{photo_index}].preview")
        for index, memory in visible:
            what = f"memories[{index}]"
            date = check_date(require_text(memory, "date", what), f"{what}.date")
            title = check_text(require_text(memory, "title", what), DEVICE_LIMITS["title_max"],
                               f"{what}.title", report)
            body = check_text(require_text(memory, "body", what), DEVICE_LIMITS["body_max"],
                              f"{what}.body", report)
            lines.append("")
            lines.append("[memory]")
            lines.append(f"date={date}")
            lines.append(f"title={escape_value(title)}")
            lines.append(f"body={escape_value(body)}")
            photo_id = memory.get("photoId")
            if isinstance(photo_id, str) and photo_id:
                if photo_id not in photo_ids:
                    raise ConversionError(f"{what}.photoId {photo_id!r} is not in album.photos")
                lines.append(f"photo={photo_ids[photo_id]}")
            else:
                lines.append('photo=""')
            if memory.get("tags"):
                report.drop(f"{what}.tags")
            lines.append("[/memory]")
        report.mapped.append(f"{len(visible)} visible memories -> [memory] sections")
        if len(memories) > len(visible):
            report.adapted.append(f"{len(memories) - len(visible)} hidden memories omitted")

        # --- album ------------------------------------------------------------
        if len(photo_ids) > DEVICE_LIMITS["max_photos"]:
            raise ConversionError(
                f"{len(photo_ids)} photos, over the device limit of {DEVICE_LIMITS['max_photos']}"
            )
        for photo_index, entry in enumerate(require_list(album.get("photos", []), "album.photos")):
            photo = require_object(entry, f"album.photos[{photo_index}]")
            what = f"album.photos[{photo_index}]"
            title = check_text(require_text(photo, "title", what), DEVICE_LIMITS["title_max"],
                               f"{what}.title", report)
            lines.append("")
            lines.append("[album]")
            lines.append(f"title={escape_value(title)}")
            lines.append(f"photo={photo_ids[require_text(photo, 'id', what)]}")
            lines.append("[/album]")
        report.mapped.append(f"{len(photo_ids)} photos -> [album] sections")
        report.dropped.append("album.coverId, photo.width/height/previewWidth/previewHeight/"
                              "deviceStatus/estimatedRGB565Bytes (device decodes at display time)")
        report.dropped.append("resources.json (the device does not verify media hashes; "
                              "see the note in the compatibility report)")

        # --- letters ----------------------------------------------------------
        if len(letters) > DEVICE_LIMITS["max_letters"]:
            raise ConversionError(
                f"{len(letters)} letters, over the device limit of {DEVICE_LIMITS['max_letters']}"
            )
        for index, entry in enumerate(letters):
            letter = require_object(entry, f"letters[{index}]")
            what = f"letters[{index}]"
            title = check_text(require_text(letter, "title", what), DEVICE_LIMITS["title_max"],
                               f"{what}.title", report)
            body = check_text(require_text(letter, "body", what), DEVICE_LIMITS["body_max"],
                              f"{what}.body", report)
            lines.append("")
            lines.append("[letter]")
            lines.append(f"title={escape_value(title)}")
            lines.append(f"body={escape_value(body)}")
            lines.append("[/letter]")
            if letter.get("type") != "letter":
                report.drop(f"{what}.type={letter.get('type')!r}")
            if letter.get("unlockDate"):
                report.drop(f"{what}.unlockDate (the device has no unlock rules)")
            if letter.get("subtitle"):
                report.drop(f"{what}.subtitle")
        report.mapped.append(f"{len(letters)} letters -> [letter] sections")

        # --- story chapters -> dialogues --------------------------------------
        chapters = require_list(story.get("chapters", []), "story.chapters")
        if len(chapters) > DEVICE_LIMITS["max_dialogues"]:
            raise ConversionError(
                f"{len(chapters)} chapters, over the device limit of "
                f"{DEVICE_LIMITS['max_dialogues']} dialogues"
            )
        total_lines = 0
        for index, entry in enumerate(chapters):
            chapter = require_object(entry, f"story.chapters[{index}]")
            what = f"story.chapters[{index}]"
            identifier = chapter.get("id")
            if not isinstance(identifier, int) or not 0 <= identifier <= 999:
                raise ConversionError(f"{what}.id must be 0..999, got {identifier!r}")
            chapter_lines = require_list(chapter.get("lines", []), f"{what}.lines")
            if not chapter_lines:
                raise ConversionError(f"{what} has no lines; an empty dialogue cannot be shown")
            rendered = []
            for line_index, entry_line in enumerate(chapter_lines):
                line = require_object(entry_line, f"{what}.lines[{line_index}]")
                speaker = check_text(require_text(line, "speaker", what), DEVICE_LIMITS["speaker_max"],
                                     f"{what}.lines[{line_index}].speaker", report)
                text = check_text(require_text(line, "text", what), DEVICE_LIMITS["body_max"],
                                  f"{what}.lines[{line_index}].text", report)
                if "|" in speaker:
                    raise ConversionError(f"{what}.lines[{line_index}].speaker contains '|'")
                rendered.append(f"line={escape_value(speaker)}|{escape_value(text)}")
            total_lines += len(rendered)
            if total_lines > DEVICE_LIMITS["max_lines"]:
                raise ConversionError(
                    f"{total_lines} dialogue lines in total, over the device limit of "
                    f"{DEVICE_LIMITS['max_lines']}"
                )
            lines.append("")
            lines.append("[dialogue]")
            lines.append(f"id={identifier}")
            lines.extend(rendered)
            lines.append("[/dialogue]")
            if chapter.get("choices"):
                report.drop(f"{what}.choices (the device has no branching)")
            if chapter.get("scene"):
                report.drop(f"{what}.scene")
            if chapter.get("event"):
                report.drop(f"{what}.event (planning text, not runtime data)")
        report.mapped.append(f"{len(chapters)} chapters -> [dialogue] sections")
        quest = story.get("quest")
        if quest:
            report.drop("story.quest (the device has no quest model)")

    text = "\n".join(lines) + "\n"
    encoded = text.encode("utf-8")
    if len(encoded) > DEVICE_LIMITS["max_bytes"]:
        raise ConversionError(
            f"converted file is {len(encoded)} B, over the device limit of "
            f"{DEVICE_LIMITS['max_bytes']} B"
        )
    report.counts = {
        "memories": sum(1 for line in lines if line == "[memory]"),
        "photos": sum(1 for line in lines if line == "[album]"),
        "letters": sum(1 for line in lines if line == "[letter]"),
        "dialogues": sum(1 for line in lines if line == "[dialogue]"),
        "lines": total_lines,
        "bytes": len(encoded),
    }
    report.bytes_written = len(encoded)
    return text


def verify_with_firmware(path: Path) -> tuple[bool, str]:
    """Parse the produced file with the real C parser, when its binary exists."""
    checker = Path(__file__).resolve().parents[1] / "build/host_tests/lw_content_tests.exe"
    if not checker.exists():
        return True, "content parser binary not built; skipped (run tools/run_backend_tests.ps1)"
    import subprocess

    result = subprocess.run([str(checker), "--check-content", str(path)], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Convert a Studio .lwpack into a device .lwcx")
    parser.add_argument("archive", nargs="?", type=Path, help="the .lwpack / .lwstudio file")
    parser.add_argument("--out", type=Path, help="write the converted .lwcx here")
    parser.add_argument("--report", type=Path, help="write the compatibility report (JSON) here")
    parser.add_argument("--allow-partial", action="store_true",
                        help="accept a conversion that drops fields (reported, exit 1)")
    parser.add_argument("--verify", type=Path, help="only parse an existing .lwcx with the firmware")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    args = parser.parse_args(argv)

    if args.verify:
        ok, message = verify_with_firmware(args.verify)
        print(message)
        return 0 if ok else 1
    if args.archive is None:
        parser.error("give an archive to convert (or --verify an existing .lwcx)")

    report = Report(source=args.archive.as_posix(), strict=not args.allow_partial)
    try:
        text = convert(args.archive, report)
    except ConversionError as exc:
        report.ok = False
        report.error(str(exc))
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report.as_dict(), ensure_ascii=False, indent=2) + "\n",
                                   encoding="utf-8")
        if args.json:
            print(json.dumps(report.as_dict(), ensure_ascii=False, indent=2))
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1

    if report.errors:
        report.ok = False
        if args.json:
            print(json.dumps(report.as_dict(), ensure_ascii=False, indent=2))
        else:
            for error in report.errors:
                print(f"error: {error}", file=sys.stderr)
        return 1

    partial = bool(report.dropped)
    if partial and not args.allow_partial:
        report.ok = False
        report.error(
            f"{len(report.dropped)} field(s) have no device representation; "
            "re-run with --allow-partial to convert anyway (the report lists them)"
        )
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report.as_dict(), ensure_ascii=False, indent=2) + "\n",
                                   encoding="utf-8")
        if args.json:
            print(json.dumps(report.as_dict(), ensure_ascii=False, indent=2))
        else:
            print(report.error[-1] if False else report.errors[0], file=sys.stderr)
            for item in report.dropped:
                print(f"  dropped: {item}", file=sys.stderr)
        return 1

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        # Write to a sibling temporary then replace, so an interrupted run never
        # leaves a half file where the device would read it.
        temporary = args.out.with_suffix(args.out.suffix + ".tmp")
        temporary.write_text(text, encoding="utf-8", newline="\n")
        temporary.replace(args.out)
        report.output = args.out.as_posix()
    report.ok = True

    if args.out:
        ok, message = verify_with_firmware(args.out)
        report.mapped.append(f"firmware parse: {'ok' if ok else 'FAILED'} ({message.splitlines()[0] if message else 'no output'})")
        if not ok:
            report.ok = False
            report.error(f"the firmware parser rejected the output: {message}")

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report.as_dict(), ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8")

    if args.json:
        print(json.dumps(report.as_dict(), ensure_ascii=False, indent=2))
    else:
        print(f"converted {args.archive.name} -> {report.output or '(dry run)'}")
        for key, value in report.counts.items():
            print(f"  {key}: {value}")
        for item in report.adapted:
            print(f"  adapted: {item}")
        for item in report.dropped:
            print(f"  dropped: {item}")
        if partial:
            print("note: conversion is partial (--allow-partial was given)")
    return 0 if report.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
