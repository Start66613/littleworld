import { createHash } from "node:crypto";
import { zipSync, unzipSync, strToU8 } from "fflate";
import { LIMITS, SCHEMA, parseProject, safePath } from "../shared/model.ts";
import type { Project, Snapshot } from "../shared/model.ts";
import { textAdapterIssues } from "../shared/compatibility.ts";

const sections = [
  "manifest.json",
  "anniversary.json",
  "letters.json",
  "memories.json",
  "album.json",
  "story.json",
  "resources.json",
];
export const sha256 = (bytes: Uint8Array) =>
  createHash("sha256").update(bytes).digest("hex");
export function imageMime(bytes: Uint8Array): string {
  if (
    bytes.length >= 24 &&
    Buffer.from(bytes.subarray(0, 8)).equals(
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    )
  )
    return "image/png";
  if (
    bytes.length >= 12 &&
    bytes[0] === 255 &&
    bytes[1] === 216 &&
    bytes[2] === 255
  )
    return "image/jpeg";
  if (
    bytes.length >= 20 &&
    Buffer.from(bytes.subarray(0, 4)).toString() === "RIFF" &&
    Buffer.from(bytes.subarray(8, 12)).toString() === "WEBP"
  )
    return "image/webp";
  throw new Error("仅支持 PNG、JPEG、WebP 位图；文件内容与格式不符");
}
export function verifySnapshot(input: unknown): Snapshot {
  if (
    !input ||
    typeof input !== "object" ||
    Object.keys(input).sort().join(",") !== "media,project"
  )
    throw new Error("项目结构不完整");
  const obj = input as Snapshot,
    project = parseProject(obj.project),
    media = obj.media;
  if (
    !media ||
    typeof media !== "object" ||
    Array.isArray(media) ||
    Object.keys(media).length !== project.resources.length
  )
    throw new Error("媒体清单不完整");
  const result: Record<string, string> = Object.create(null);
  for (const r of project.resources) {
    const b64 = media[r.path];
    if (
      typeof b64 !== "string" ||
      b64.length > Math.ceil(LIMITS.file / 3) * 4 ||
      b64.length % 4 !== 0 ||
      !/^[A-Za-z0-9+/]*={0,2}$/.test(b64)
    )
      throw new Error("资源编码无效");
    const bytes = Buffer.from(b64, "base64");
    if (
      bytes.length !== r.bytes ||
      sha256(bytes) !== r.sha256 ||
      imageMime(bytes) !== r.mime
    )
      throw new Error("资源损坏：大小、SHA-256 或图片格式不匹配");
    result[r.path] = b64;
  }
  return { project, media: result };
}
export function pack(input: Snapshot): Buffer {
  const { project: p, media } = verifySnapshot(input);
  const { anniversary, letters, memories, album, story, resources, ...meta } =
    p;
  const docs: Record<string, unknown> = {
    "manifest.json": {
      ...meta,
      compatibility: "studio-only; firmware reader not implemented",
    },
    "anniversary.json": anniversary,
    "letters.json": letters,
    "memories.json": memories,
    "album.json": album,
    "story.json": story,
    "resources.json": resources,
  };
  const entries: Record<string, Uint8Array> = {};
  let jsonSize = 0;
  for (const [path, value] of Object.entries(docs)) {
    const bytes = strToU8(JSON.stringify(value, null, 2));
    jsonSize += bytes.length;
    entries[path] = bytes;
  }
  if (jsonSize > LIMITS.json) throw new Error("文字数据超出 4 MiB，请拆分项目");
  for (const r of resources)
    entries[r.path] = Buffer.from(media[r.path], "base64");
  const out = Buffer.from(zipSync(entries, { level: 0 }));
  if (out.length > LIMITS.archive) throw new Error("内容包超出 64 MiB");
  return out;
}
function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
// Preflight central directory BEFORE decompression. No extraction onto disk.
function inspectZip(
  buffer: Buffer,
): Map<string, { size: number; crc: number }> {
  if (buffer.length < 22 || buffer.length > LIMITS.archive)
    throw new Error("内容包大小无效（上限 64 MiB）");
  let end = -1;
  for (let i = buffer.length - 22; i >= Math.max(0, buffer.length - 65557); i--)
    if (
      buffer.readUInt32LE(i) === 0x06054b50 &&
      i + 22 + buffer.readUInt16LE(i + 20) === buffer.length
    ) {
      end = i;
      break;
    }
  if (end < 0) throw new Error("ZIP 文件不完整或已损坏");
  const count = buffer.readUInt16LE(end + 10),
    offset = buffer.readUInt32LE(end + 16),
    length = buffer.readUInt32LE(end + 12);
  if (
    buffer.readUInt16LE(end + 4) ||
    buffer.readUInt16LE(end + 6) ||
    buffer.readUInt16LE(end + 8) !== count ||
    count > 87 ||
    count < 7 ||
    offset + length !== end
  )
    throw new Error("不支持分卷、ZIP64 或异常 ZIP 目录");
  const files = new Map<string, { size: number; crc: number }>();
  let pos = offset,
    total = 0,
    jsonTotal = 0;
  for (let i = 0; i < count; i++) {
    if (pos + 46 > end || buffer.readUInt32LE(pos) !== 0x02014b50)
      throw new Error("ZIP 目录损坏");
    const flags = buffer.readUInt16LE(pos + 8),
      method = buffer.readUInt16LE(pos + 10),
      compressed = buffer.readUInt32LE(pos + 20),
      size = buffer.readUInt32LE(pos + 24),
      nl = buffer.readUInt16LE(pos + 28),
      el = buffer.readUInt16LE(pos + 30),
      cl = buffer.readUInt16LE(pos + 32),
      local = buffer.readUInt32LE(pos + 42);
    const name = buffer.subarray(pos + 46, pos + 46 + nl).toString("utf8");
    if (
      pos + 46 + nl + el + cl > end ||
      files.has(name) ||
      !(sections.includes(name) || safePath(name))
    )
      throw new Error("重复文件名或不安全路径（拒绝路径穿越）");
    if (
      flags & 1 ||
      ![0, 8].includes(method) ||
      size > (name.endsWith(".json") ? LIMITS.json : LIMITS.file)
    )
      throw new Error("资源超限或 ZIP 压缩方式不支持");
    if (local + 30 > offset || buffer.readUInt32LE(local) !== 0x04034b50)
      throw new Error("ZIP 本地文件头损坏");
    const lnl = buffer.readUInt16LE(local + 26),
      lel = buffer.readUInt16LE(local + 28);
    if (
      buffer.subarray(local + 30, local + 30 + lnl).toString("utf8") !== name ||
      local + 30 + lnl + lel + compressed > offset ||
      buffer.readUInt16LE(local + 8) !== method
    )
      throw new Error("ZIP 文件头不一致");
    total += size;
    if (name.endsWith(".json")) jsonTotal += size;
    if (total > LIMITS.totalMedia + LIMITS.json || jsonTotal > LIMITS.json)
      throw new Error("解压后内容过大");
    files.set(name, { size, crc: buffer.readUInt32LE(pos + 16) });
    pos += 46 + nl + el + cl;
  }
  if (pos !== end) throw new Error("ZIP 目录长度错误");
  return files;
}
export function unpack(input: Uint8Array): Snapshot {
  const buffer = Buffer.from(input),
    inventory = inspectZip(buffer);
  const files = unzipSync(buffer, {
    filter: (f) => {
      const expected = inventory.get(f.name);
      if (!expected || f.originalSize !== expected.size)
        throw new Error("ZIP 展开大小不一致");
      return true;
    },
  });
  for (const [name, entry] of inventory)
    if (
      !files[name] ||
      files[name].length !== entry.size ||
      crc32(files[name]) !== entry.crc
    )
      throw new Error("文件校验失败，内容包可能损坏");
  const json = (name: string) => {
    if (!files[name]) throw new Error(`内容包缺少 ${name}`);
    try {
      return JSON.parse(
        new TextDecoder("utf-8", { fatal: true }).decode(files[name]),
      );
    } catch {
      throw new Error(`${name} 不是有效的 UTF-8 JSON`);
    }
  };
  const manifest = json("manifest.json");
  if (
    manifest.schema !== SCHEMA ||
    manifest.compatibility !== "studio-only; firmware reader not implemented"
  )
    throw new Error("不支持的内容包版本或兼容声明");
  delete manifest.compatibility;
  const project = parseProject({
    ...manifest,
    anniversary: json("anniversary.json"),
    letters: json("letters.json"),
    memories: json("memories.json"),
    album: json("album.json"),
    story: json("story.json"),
    resources: json("resources.json"),
  });
  const media: Record<string, string> = Object.create(null);
  if (inventory.size !== sections.length + project.resources.length)
    throw new Error("内容包包含清单外文件");
  for (const r of project.resources) {
    if (!files[r.path]) throw new Error("内容包缺少资源");
    media[r.path] = Buffer.from(files[r.path]).toString("base64");
  }
  return verifySnapshot({ project, media });
}
export function emitC(p: Project): string {
  p = parseProject(p);
  const issues = textAdapterIssues(p);
  if (issues.length)
    throw new Error("设备文本适配被拒绝：\n" + issues.join("\n"));
  const c = (s: string) =>
    '"' +
    [...Buffer.from(s, "utf8")]
      .map((b) => `\\${b.toString(8).padStart(3, "0")}`)
      .join("") +
    '"';
  const memories = p.memories.filter((x) => x.visible);
  const lines = [
    "/* Generated by Little World Studio. Text adapter only; NO firmware package reader.",
    " * Keep this file outside firmware until integrated and tested. Photo paths intentionally empty.",
    " * Scene / event / quest notes are metadata, not executable hotspots. */",
    "#pragma once",
    '#include "lw/lw_memories.h"',
    '#include "lw_rpg.h"',
    "",
  ];
  if (memories.length)
    lines.push(
      "static const lw_memory_t studio_memories[] = {",
      ...memories.map(
        (m) =>
          `    {{${m.date.split("-").map(Number).join(",")}}, ${c(m.title)}, ${c(m.body)}, ""},`,
      ),
      "};",
    );
  lines.push(`#define STUDIO_MEMORY_COUNT ${memories.length}`, "");
  for (const ch of p.story.chapters) {
    lines.push(
      `static const lw_rpg_line_t studio_lines_${ch.id}[] = {`,
      ...ch.lines.map((l) => `    {${c(l.speaker)}, ${c(l.text)}},`),
      "};",
    );
    if (ch.choices.length)
      lines.push(
        `static const lw_rpg_choice_t studio_choices_${ch.id}[] = {`,
        ...ch.choices.map((o) => `    {${c(o.text)}, ${o.nextId}u},`),
        "};",
      );
  }
  if (p.story.chapters.length)
    lines.push(
      "static const lw_rpg_dialogue_t studio_dialogues[] = {",
      ...p.story.chapters.map(
        (ch) =>
          `    {${ch.id}u, ${ch.lines.length}, studio_lines_${ch.id}, ${ch.choices.length}, ${ch.choices.length ? `studio_choices_${ch.id}` : "NULL"}},`,
      ),
      "};",
    );
  lines.push(`#define STUDIO_DIALOGUE_COUNT ${p.story.chapters.length}`, "");
  return lines.join("\n");
}
