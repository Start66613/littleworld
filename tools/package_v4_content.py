"""Little World V4.2 - content package builder / checker.

The device reads ONE text file from the card:

    littleworld/content.lwcx

Its grammar lives in firmware/components/lw_content/include/lw_content.h. This
tool is the other half of that contract: it takes a directory of human-edited
`.lwcx` fragments (or one file), validates everything the firmware would
reject, and writes the single file the device consumes.

Why validate on the PC: a bad byte on the card costs a boot-time rejection the
user cannot see. Here it costs a line number and a sentence.

Nothing in this tool invents content. Every string written out comes from the
input files. The sample shipped in the repository is labelled 示例内容.

Usage:
    python tools/package_v4_content.py                     # build from content/
    python tools/package_v4_content.py --check             # validate only
    python tools/package_v4_content.py --source <dir|file> --out <dir>
    python tools/package_v4_content.py --init <dir>        # write a template

Exit codes: 0 ok, 1 validation failed, 2 usage error.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import re
import sys
import unicodedata
from pathlib import Path

# ---------------------------------------------------------------------------
# Limits. Keep in sync with firmware/components/lw_content/include/lw_content.h
# ---------------------------------------------------------------------------
MAGIC = "LWCX0001"
SCHEMA = 1
CONTENT_VERSION = 1
MAX_BYTES = 32768
MAX_MEMORIES = 32
MAX_PHOTOS = 64
MAX_LETTERS = 8
MAX_DIALOGUES = 16
MAX_LINES = 64
TITLE_MAX = 128
BODY_MAX = 512
PATH_MAX = 96
SPEAKER_MAX = 24
DIALOGUE_ID_MAX = 999

FILE_NAME = "content.lwcx"

_DATE_RE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})$")
_BAD_PATH_CHARS = set('\\:*?"<>|')


class Problem(Exception):
    """One validation failure, reported with the file and line that caused it."""

    def __init__(self, where: str, line: int, message: str) -> None:
        super().__init__(f"{where}:{line}: {message}")
        self.where = where
        self.line = line
        self.message = message


# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------
def _valid_date(text: str) -> bool:
    m = _DATE_RE.match(text)
    if not m:
        return False
    year, month, day = (int(g) for g in m.groups())
    if year < 1970 or year > 9999:
        return False
    try:
        _dt.date(year, month, day)
    except ValueError:
        return False
    return True


def _text_problem(text: str, limit: int, label: str) -> str | None:
    """Everything the firmware would reject about one value, as advice."""
    if len(text.encode("utf-8")) > limit:
        return f"{label} is {len(text.encode('utf-8'))} bytes, over the {limit}-byte limit"
    for ch in text:
        if ch in "\r\n":
            continue
        if unicodedata.category(ch) in ("Cc", "Cs"):
            return f"{label} contains a control character U+{ord(ch):04X}"
    if text.count("\n") > 8:
        return f"{label} has {text.count(chr(10))} line breaks, the device draws at most 8"
    return None


def _path_problem(path: str) -> str | None:
    if not path:
        return "photo path is empty"
    if len(path) > PATH_MAX:
        return f"photo path is {len(path)} characters, over the {PATH_MAX} limit"
    if path.startswith("/") or path.startswith("\\"):
        return "photo path must be relative to the littleworld/ folder, not absolute"
    if ".." in path.split("/"):
        return "photo path must not contain '..'"
    bad = sorted(set(path) & _BAD_PATH_CHARS)
    if bad:
        return f"photo path contains characters a FAT card cannot store: {' '.join(bad)}"
    if not path.lower().endswith(".lwph"):
        return "photo path should end in .lwph (the device's picture container)"
    return None


# ---------------------------------------------------------------------------
# A tiny model of what a content file contains, built while validating
# ---------------------------------------------------------------------------
class Content:
    def __init__(self) -> None:
        self.schema: int | None = None
        self.version: int | None = None
        self.anniversary: str | None = None
        self.welcome: str | None = None
        self.memories: list[dict] = []
        self.photos: list[dict] = []
        self.letters: list[dict] = []
        self.dialogues: list[dict] = []
        self.lines: int = 0
        self.files: list[str] = []
        self.warnings: list[str] = []


def _decode(raw: bytes, where: str) -> str:
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise Problem(where, 1, f"not valid UTF-8 at byte {exc.start}: {exc.reason}") from None
    if "\x00" in text:
        raise Problem(where, 1, "file contains a NUL byte (saved as UTF-16 instead of UTF-8?)")
    return text


def parse_file(path: Path) -> Content:
    """Parse one .lwcx fragment. Comments and blank lines are ignored."""
    where = path.name
    raw = path.read_bytes()
    if len(raw) > MAX_BYTES:
        raise Problem(where, 1, f"file is {len(raw)} bytes, over the {MAX_BYTES}-byte limit")
    text = _decode(raw, where)

    content = Content()
    content.files.append(str(path))

    section: str | None = None
    current: dict = {}
    seen_magic = False
    line_no = 0

    def close(line_no: int) -> None:
        nonlocal section, current
        if section is None:
            return
        if section == "memory":
            for key in ("date", "title", "body"):
                if key not in current:
                    raise Problem(where, line_no, f"[memory] is missing '{key}'")
            content.memories.append(current)
        elif section == "album":
            for key in ("title", "photo"):
                if key not in current:
                    raise Problem(where, line_no, f"[album] is missing '{key}'")
            content.photos.append(current)
        elif section == "letter":
            for key in ("title", "body"):
                if key not in current:
                    raise Problem(where, line_no, f"[letter] is missing '{key}'")
            content.letters.append(current)
        elif section == "dialogue":
            if "id" not in current:
                raise Problem(where, line_no, "[dialogue] is missing 'id'")
            if not current.get("lines"):
                raise Problem(where, line_no, "[dialogue] has no 'line' entries")
            content.dialogues.append(current)
        section = None
        current = {}

    for raw_line in text.splitlines():
        line_no += 1
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if not seen_magic:
            if line != MAGIC:
                raise Problem(where, line_no, f"first line must be {MAGIC}")
            seen_magic = True
            continue
        if line.startswith("["):
            if not line.endswith("]"):
                raise Problem(where, line_no, "section tag is not closed with ]")
            name = line[1:-1]
            if name.startswith("/"):
                want = section
                if want is None:
                    raise Problem(where, line_no, f"closing tag [/{name[1:]}] with nothing open")
                if name[1:] != want:
                    raise Problem(where, line_no, f"closing tag [/{name[1:]}] does not match [{want}]")
                close(line_no)
            else:
                if section is not None:
                    raise Problem(where, line_no, f"[{name}] opened inside [{section}]")
                if name not in ("memory", "album", "letter", "dialogue"):
                    raise Problem(where, line_no, f"unknown section [{name}]")
                section = name
                current = {}
            continue
        if "=" not in line:
            raise Problem(where, line_no, "expected key=value")
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()

        if section is None:
            if key == "schema":
                try:
                    content.schema = int(value)
                except ValueError:
                    raise Problem(where, line_no, "schema must be an integer") from None
                if content.schema != SCHEMA:
                    raise Problem(where, line_no, f"schema {content.schema} is not supported (want {SCHEMA})")
            elif key == "content_version":
                try:
                    content.version = int(value)
                except ValueError:
                    raise Problem(where, line_no, "content_version must be an integer") from None
                if not 1 <= content.version <= CONTENT_VERSION:
                    raise Problem(where, line_no,
                                  f"content_version {content.version} is not supported "
                                  f"(1..{CONTENT_VERSION})")
            elif key == "anniversary":
                if not _valid_date(value):
                    raise Problem(where, line_no, f"anniversary '{value}' is not a real date (YYYY-MM-DD)")
                content.anniversary = value
            elif key == "welcome":
                problem = _text_problem(value, BODY_MAX, "welcome")
                if problem:
                    raise Problem(where, line_no, problem)
                content.welcome = value
            else:
                raise Problem(where, line_no, f"unknown key '{key}' outside a section")
            continue

        if section == "memory":
            if key == "date":
                if not _valid_date(value):
                    raise Problem(where, line_no, f"date '{value}' is not a real date (YYYY-MM-DD)")
                current["date"] = value
            elif key == "title":
                problem = _text_problem(value, TITLE_MAX, "title")
                if problem:
                    raise Problem(where, line_no, problem)
                if not value:
                    raise Problem(where, line_no, "title is empty")
                current["title"] = value
            elif key == "body":
                problem = _text_problem(value, BODY_MAX, "body")
                if problem:
                    raise Problem(where, line_no, problem)
                if not value:
                    raise Problem(where, line_no, "body is empty")
                current["body"] = value
            elif key == "photo":
                if value:
                    problem = _path_problem(value)
                    if problem:
                        raise Problem(where, line_no, problem)
                current["photo"] = value
            else:
                raise Problem(where, line_no, f"unknown key '{key}' in [memory]")
        elif section == "album":
            if key == "title":
                problem = _text_problem(value, TITLE_MAX, "title")
                if problem:
                    raise Problem(where, line_no, problem)
                if not value:
                    raise Problem(where, line_no, "title is empty")
                current["title"] = value
            elif key == "photo":
                problem = _path_problem(value)
                if problem:
                    raise Problem(where, line_no, problem)
                current["photo"] = value
            else:
                raise Problem(where, line_no, f"unknown key '{key}' in [album]")
        elif section == "letter":
            if key == "title":
                problem = _text_problem(value, TITLE_MAX, "title")
                if problem:
                    raise Problem(where, line_no, problem)
                if not value:
                    raise Problem(where, line_no, "title is empty")
                current["title"] = value
            elif key == "body":
                problem = _text_problem(value, BODY_MAX, "body")
                if problem:
                    raise Problem(where, line_no, problem)
                if not value:
                    raise Problem(where, line_no, "body is empty")
                current["body"] = value
            else:
                raise Problem(where, line_no, f"unknown key '{key}' in [letter]")
        elif section == "dialogue":
            if key == "id":
                try:
                    ident = int(value)
                except ValueError:
                    raise Problem(where, line_no, "id must be an integer") from None
                if not 0 <= ident <= DIALOGUE_ID_MAX:
                    raise Problem(where, line_no, f"id {ident} is outside 0..{DIALOGUE_ID_MAX}")
                current["id"] = ident
            elif key == "line":
                if "|" not in value:
                    raise Problem(where, line_no, "line must be 'speaker|text'")
                speaker, body = value.split("|", 1)
                speaker = speaker.strip()
                body = body.strip()
                if not speaker:
                    raise Problem(where, line_no, "line has an empty speaker")
                if not body:
                    raise Problem(where, line_no, "line has an empty text")
                problem = _text_problem(speaker, SPEAKER_MAX, "speaker")
                if problem:
                    raise Problem(where, line_no, problem)
                problem = _text_problem(body, BODY_MAX, "line text")
                if problem:
                    raise Problem(where, line_no, problem)
                current.setdefault("lines", []).append((speaker, body))
            else:
                raise Problem(where, line_no, f"unknown key '{key}' in [dialogue]")

    if not seen_magic:
        raise Problem(where, 1, f"file is empty or does not start with {MAGIC}")
    if section is not None:
        raise Problem(where, line_no, f"[{section}] is never closed")
    if content.version is None:
        raise Problem(where, 1, "content_version is missing")
    if content.schema is None:
        raise Problem(where, 1, "schema is missing")
    return content


def merge(parts: list[Content], where: str) -> Content:
    """Combine fragments in filename order into the single device file."""
    total = Content()
    total.schema = SCHEMA
    total.version = CONTENT_VERSION
    for part in parts:
        total.files.extend(part.files)
        total.warnings.extend(part.warnings)
        if part.anniversary:
            if total.anniversary and total.anniversary != part.anniversary:
                raise Problem(where, 1,
                              f"anniversary is set twice: {total.anniversary} and {part.anniversary}")
            total.anniversary = part.anniversary
        if part.welcome:
            if total.welcome:
                total.welcome += "\\n" + part.welcome
            else:
                total.welcome = part.welcome
        total.memories.extend(part.memories)
        total.photos.extend(part.photos)
        total.letters.extend(part.letters)
        total.dialogues.extend(part.dialogues)

    limits = (
        ("memories", len(total.memories), MAX_MEMORIES),
        ("photos", len(total.photos), MAX_PHOTOS),
        ("letters", len(total.letters), MAX_LETTERS),
        ("dialogues", len(total.dialogues), MAX_DIALOGUES),
    )
    for label, count, limit in limits:
        if count > limit:
            raise Problem(where, 1, f"{count} {label}, over the limit of {limit}")

    ids = [d["id"] for d in total.dialogues]
    duplicates = sorted({i for i in ids if ids.count(i) > 1})
    if duplicates:
        raise Problem(where, 1, f"duplicate dialogue id(s): {', '.join(str(d) for d in duplicates)}")

    total.lines = sum(len(d.get("lines", [])) for d in total.dialogues)
    if total.lines > MAX_LINES:
        raise Problem(where, 1, f"{total.lines} dialogue lines, over the limit of {MAX_LINES}")

    # The device sorts the timeline itself, but an out-of-order file is almost
    # always a typo, so it is worth a warning rather than silence.
    dates = [m["date"] for m in total.memories]
    if dates != sorted(dates):
        total.warnings.append("memories are not in date order; the device will sort them, "
                              "but check that the dates are what you meant")
    return total


# ---------------------------------------------------------------------------
# Emitting the device file
# ---------------------------------------------------------------------------
def _escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\n", "\\n")


def render(content: Content) -> str:
    out: list[str] = [MAGIC, f"schema={content.schema}", f"content_version={content.version}"]
    if content.anniversary:
        out.append(f"anniversary={content.anniversary}")
    if content.welcome:
        out.append(f"welcome={_escape(content.welcome)}")
    out.append("")

    for memory in content.memories:
        out.append("[memory]")
        out.append(f"date={memory['date']}")
        out.append(f"title={_escape(memory['title'])}")
        out.append(f"body={_escape(memory['body'])}")
        if memory.get("photo"):
            out.append(f"photo={memory['photo']}")
        out.append("[/memory]")
        out.append("")

    for photo in content.photos:
        out.append("[album]")
        out.append(f"title={_escape(photo['title'])}")
        out.append(f"photo={photo['photo']}")
        out.append("[/album]")
        out.append("")

    for letter in content.letters:
        out.append("[letter]")
        out.append(f"title={_escape(letter['title'])}")
        out.append(f"body={_escape(letter['body'])}")
        out.append("[/letter]")
        out.append("")

    for dialogue in content.dialogues:
        out.append("[dialogue]")
        out.append(f"id={dialogue['id']}")
        for speaker, body in dialogue.get("lines", []):
            out.append(f"line={_escape(speaker)}|{_escape(body)}")
        out.append("[/dialogue]")
        out.append("")

    text = "\n".join(out).rstrip("\n") + "\n"
    return text


def write_atomic(path: Path, data: bytes) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_bytes(data)
    tmp.replace(path)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
TEMPLATE = """# Little World 内容文件（模板）
#
# 用法：
#   1. 用记事本 / VS Code 打开，按下面的格式改成你自己的内容。
#   2. 存成 UTF-8 编码（不要存成 UTF-16）。
#   3. 运行 python tools/package_v4_content.py --check 先检查一遍。
#   4. 运行 python tools/package_v4_content.py 生成设备用的 content.lwcx。
#
# 规则（写错会在第 3 步被指出来，不会带到设备上）：
#   - 日期必须是真实存在的 YYYY-MM-DD，且不早于 1970-01-01。
#   - 标题最长 {title} 字节，正文最长 {body} 字节（一个汉字 3 字节）。
#   - 照片路径相对 littleworld/ 文件夹，不能含 ..，以 .lwph 结尾。
#   - 最多 {memories} 条回忆、{photos} 张相册索引、{letters} 封信、{dialogues} 段对话。
#   - 下面所有文字都是示例，不是你们的真实经历，请自行替换或删除。
#
LWCX0001
schema=1
content_version=1
anniversary=2024-10-25
welcome=这里收着我们一起走过的日子。\\n慢一点翻。

[memory]
date=2024-10-25
title=示例：故事开始
body=示例内容：这一天我们在心里定下了一个小约定。把这里换成你们真正的第一天。
[/memory]

[memory]
date=2025-02-14
title=示例：第一个情人节
body=示例内容：一起吃了顿很慢的晚饭，然后在路灯下走了很久。
[/memory]

[album]
title=示例：海边
photo=photos/001.lwph
[/album]

[letter]
title=示例：给未来的你
body=示例内容：这封信还没有写完。
[/letter]

[dialogue]
id=10
line=男方|示例：照片盒里有一叠空白的相纸。
line=qianqian|示例：先把它们放进第一页吧。
[/dialogue]
""".format(title=TITLE_MAX, body=BODY_MAX, memories=MAX_MEMORIES,
           photos=MAX_PHOTOS, letters=MAX_LETTERS, dialogues=MAX_DIALOGUES)


def collect(source: Path) -> list[Path]:
    if source.is_file():
        return [source]
    if not source.is_dir():
        raise SystemExit(f"source not found: {source}")
    files = sorted(p for p in source.glob("*.lwcx") if p.is_file())
    if not files:
        raise SystemExit(f"no .lwcx file in {source}")
    return files


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build or check the V4.2 content file.")
    parser.add_argument("--source", default="content", help="a directory of .lwcx files, or one file")
    parser.add_argument("--out", default="dist/littleworld-v4-sd/littleworld",
                        help="directory that receives content.lwcx")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--init", metavar="DIR", help="write a commented template into DIR and exit")
    parser.add_argument("--json", action="store_true", help="print a machine-readable report")
    args = parser.parse_args(argv)

    if args.init:
        target = Path(args.init)
        target.mkdir(parents=True, exist_ok=True)
        path = target / "content.lwcx"
        if path.exists():
            print(f"refusing to overwrite {path}")
            return 2
        path.write_text(TEMPLATE, encoding="utf-8", newline="\n")
        print(f"template written: {path}")
        print("edit it, then run: python tools/package_v4_content.py --source " + str(target))
        return 0

    source = Path(args.source)
    try:
        files = collect(source)
        parts = [parse_file(path) for path in files]
        content = merge(parts, str(source))
        text = render(content)
    except Problem as problem:
        print(f"[FAIL] {problem.where}:{problem.line}: {problem.message}")
        if args.json:
            print(json.dumps({"ok": False, "file": problem.where, "line": problem.line,
                              "error": problem.message}, ensure_ascii=False))
        return 1

    data = text.encode("utf-8")
    if len(data) > MAX_BYTES:
        print(f"[FAIL] merged file is {len(data)} bytes, over the {MAX_BYTES}-byte limit; "
              f"shorten the bodies or split the memories")
        return 1

    # Round-trip: what we wrote must parse again with the same counts. This is
    # the only check that proves render() and parse_file() agree.
    try:
        again = merge([parse_file_from_text(text, "content.lwcx")], "content.lwcx")
    except Problem as problem:
        print(f"[FAIL] generated file does not parse back: {problem.message}")
        return 1
    if (len(again.memories), len(again.photos), len(again.letters),
            len(again.dialogues), again.lines) != (len(content.memories), len(content.photos),
                                                   len(content.letters), len(content.dialogues),
                                                   content.lines):
        print("[FAIL] generated file does not round-trip: counts changed")
        return 1

    report = {
        "ok": True,
        "files": content.files,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "content_version": content.version,
        "anniversary": content.anniversary,
        "memories": len(content.memories),
        "photos": len(content.photos),
        "letters": len(content.letters),
        "dialogues": len(content.dialogues),
        "lines": content.lines,
        "warnings": content.warnings,
        "output": None if args.check else str(Path(args.out) / FILE_NAME),
    }

    if args.check:
        print(f"[OK] {len(files)} file(s), {len(data)} bytes, "
              f"{report['memories']} memories, {report['photos']} photos, "
              f"{report['letters']} letters, {report['dialogues']} dialogues")
    else:
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        target = out / FILE_NAME
        write_atomic(target, data)
        print(f"[OK] wrote {target} ({len(data)} bytes, sha256 {report['sha256'][:16]}…)")

    for warning in content.warnings:
        print(f"[WARN] {warning}")
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


def parse_file_from_text(text: str, where: str) -> Content:
    """Same parser as parse_file(), for a buffer that never touched the disk."""
    import tempfile

    with tempfile.NamedTemporaryFile("wb", suffix=".lwcx", delete=False) as handle:
        handle.write(text.encode("utf-8"))
        name = handle.name
    try:
        return parse_file(Path(name))
    finally:
        Path(name).unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
