import { test } from "node:test";
import assert from "node:assert/strict";
import { zipSync, unzipSync, strToU8 } from "fflate";
import { fixture } from "./fixtures.ts";
import {
  emptyProject,
  parseProject,
  safePath,
  moveItem,
  removePhoto,
} from "../shared/model.ts";
import { pack, unpack, verifySnapshot, emitC } from "../server/package.ts";
test("empty project has no invented private content", () => {
  const p = emptyProject();
  assert.equal(p.demo, false);
  assert.equal(p.memories.length + p.letters.length + p.album.photos.length, 0);
  parseProject(p);
});
test("full package round trip including original photo bytes", () => {
  const s = fixture(true),
    out = unpack(pack(s));
  assert.deepEqual(out.project, s.project);
  assert.deepEqual({ ...out.media }, s.media);
  assert.deepEqual(unpack(pack(out)).project, s.project);
});
test("20,000 Chinese characters, emoji, multiline and C quoting survive", () => {
  const s = fixture();
  s.project.letters[0].body = "中".repeat(19980) + '💌\n"\\结尾';
  const out = unpack(pack(s));
  assert.equal(out.project.letters[0].body, s.project.letters[0].body);
  assert.match(emitC(out.project), /studio_dialogues/);
  assert.ok(!emitC(out.project).includes("DEMO */"));
});
test("oversize text is rejected", () => {
  const s = fixture();
  s.project.letters[0].body = "中".repeat(20001);
  assert.throws(() => pack(s));
});
test("schema version and unknown fields rejected", () => {
  const p = fixture().project;
  assert.throws(() => parseProject({ ...p, schema: "littleworld-content/99" }));
  assert.throws(() => parseProject({ ...p, futureRuntime: {} }));
});
test("missing referenced photo rejected", () => {
  const p = fixture().project;
  p.memories[0].photoId = crypto.randomUUID();
  assert.throws(() => parseProject(p), /照片不存在/);
});
test("missing media rejected", () => {
  const s = fixture(true);
  delete s.media[s.project.resources[0].path];
  assert.throws(() => pack(s), /清单/);
});
test("resource checksum tampering rejected", () => {
  const s = fixture(true),
    path = s.project.resources[0].path;
  const b = Buffer.from(s.media[path], "base64");
  b[b.length - 4] ^= 1;
  s.media[path] = b.toString("base64");
  assert.throws(() => pack(s), /损坏/);
});
test("photo deletion clears memory and cover references and resources", () => {
  const s = fixture(true),
    removed = removePhoto(s.project, s.project.album.coverId!);
  assert.equal(removed.album.photos.length, 1);
  assert.equal(removed.memories[0].photoId, null);
  assert.equal(removed.resources.length, 2);
  assert.equal(removed.album.coverId, removed.album.photos[0].id);
  parseProject(removed);
});
test("reorder handles boundaries without losing or duplicating items", () => {
  assert.deepEqual(moveItem(["一", "二", "三"], 0, 2), ["二", "三", "一"]);
  assert.deepEqual(moveItem([1, 2], -1, 0), [1, 2]);
  assert.deepEqual(moveItem([1, 2], 0, 2), [1, 2]);
});
test("RPG enforces 8 chapters, 8 lines, 2 choices, valid targets", () => {
  const p = fixture().project;
  p.story.chapters[0].choices[0].nextId = 8;
  assert.throws(() => parseProject(p), /不存在/);
  p.story.chapters[0].choices = [];
  p.story.chapters[0].lines = Array(9).fill({ speaker: "旁白", text: "文字" });
  assert.throws(() => parseProject(p));
});
test("RPG supports existing-core loop and end targets without new branch model", () => {
  const p = fixture().project;
  p.story.chapters[0].choices = [
    { text: "回到自己", nextId: 1 },
    { text: "结束", nextId: 0 },
  ];
  parseProject(p);
});
test("duplicate IDs rejected", () => {
  const p = fixture().project;
  p.letters[1].id = p.letters[0].id;
  assert.throws(() => parseProject(p), /重复/);
});
for (const name of [
  "../secret.png",
  "assets/../secret.png",
  "C:/photo.png",
  "/etc/passwd",
  "assets\\x\\original.png",
  "assets/%2e%2e/photo.png",
  "assets/x/original.png",
  "assets/00000000-0000-0000-0000-000000000000/CON.png",
  "assets/00000000-0000-0000-0000-000000000000/original.png:evil",
])
  test(`reject unsafe filename ${name}`, () =>
    assert.equal(safePath(name), false));
test("truncated archive and damaged JSON are reported", () => {
  const s = fixture(),
    bytes = pack(s);
  assert.throws(() => unpack(bytes.subarray(0, -5)), /ZIP/);
  const files = unzipSync(bytes);
  files["letters.json"] = strToU8("{broken");
  assert.throws(() => unpack(zipSync(files)), /letters.json/);
});
test("archive rejects unexpected files and traversal before decompression", () => {
  const files = unzipSync(pack(fixture()));
  files["../outside.txt"] = strToU8("bad");
  assert.throws(() => unpack(zipSync(files)), /路径/);
});
test("archive rejects corruption even when JSON is still parseable", () => {
  const b = pack(fixture());
  const i = b.indexOf(Buffer.from("Sakura"));
  const pos = i >= 0 ? i : b.indexOf(Buffer.from("DEMO"));
  b[pos] ^= 1;
  assert.throws(() => unpack(b), /校验/);
});
test("declared decompressed size is bounded before allocating", () => {
  const b = pack(fixture());
  const central = b.indexOf(Buffer.from([0x50, 0x4b, 0x01, 0x02]));
  b.writeUInt32LE(0x7fffffff, central + 24);
  assert.throws(() => unpack(b), /超限/);
});
test("invalid UTF-8 JSON is rejected", () => {
  const files = unzipSync(pack(fixture()));
  files["letters.json"] = new Uint8Array([0xff, 0xfe]);
  assert.throws(() => unpack(zipSync(files)), /UTF-8/);
});
test("resource role and RGB565 estimates must match", () => {
  const s = fixture(true);
  s.project.album.photos[0].estimatedRGB565Bytes = 123;
  assert.throws(() => verifySnapshot(s), /估算/);
});
