import { z } from "zod";
import { localToday, validDate } from "./date.ts";

export const SCHEMA = "littleworld-content/1" as const;
export const LIMITS = {
  archive: 64 * 1024 * 1024,
  totalMedia: 40 * 1024 * 1024,
  file: 10 * 1024 * 1024,
  json: 4 * 1024 * 1024,
  photos: 40,
};
const text = (n: number) =>
  z
    .string()
    .max(n)
    .refine(
      (s) => !/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(s),
      "文字包含不支持的控制字符",
    );
const date = z.string().refine(validDate, "无效日期（1970–2200，YYYY-MM-DD）");
const uid = z.string().uuid();
export function safePath(p: string): boolean {
  return (
    p.length <= 180 &&
    /^assets\/[a-f0-9-]{36}\/(original\.(png|jpg|webp)|preview\.png)$/.test(p)
  );
}
const path = z.string().refine(safePath, "不安全的资源路径");
const anniversarySchema = z
  .object({ id: uid, title: text(80), date, description: text(2000) })
  .strict();
const letterSchema = z
  .object({
    id: uid,
    type: z.enum(["today", "wish", "letter"]),
    date,
    unlockDate: date.nullable(),
    title: text(80),
    subtitle: text(160),
    body: text(20000),
  })
  .strict();
const memorySchema = z
  .object({
    id: uid,
    date,
    title: text(80),
    body: text(10000),
    photoId: uid.nullable(),
    tags: z.array(text(24)).max(12),
    visible: z.boolean(),
  })
  .strict();
const photoSchema = z
  .object({
    id: uid,
    title: text(80),
    original: path,
    preview: path,
    width: z.number().int().min(1).max(20000),
    height: z.number().int().min(1).max(20000),
    previewWidth: z.number().int().min(1).max(1024),
    previewHeight: z.number().int().min(1).max(600),
    deviceStatus: z.literal("pending"),
    estimatedRGB565Bytes: z.number().int().positive().max(1228800),
  })
  .strict();
const resourceSchema = z
  .object({
    path,
    role: z.enum(["original", "preview"]),
    mime: z.enum(["image/png", "image/jpeg", "image/webp"]),
    bytes: z.number().int().positive().max(LIMITS.file),
    sha256: z.string().regex(/^[a-f0-9]{64}$/),
  })
  .strict();
const chapterSchema = z
  .object({
    id: z.number().int().min(1).max(8),
    title: text(80),
    scene: text(80),
    event: text(500),
    lines: z
      .array(z.object({ speaker: text(40), text: text(1000) }).strict())
      .min(1)
      .max(8),
    choices: z
      .array(
        z
          .object({ text: text(80), nextId: z.number().int().min(0).max(8) })
          .strict(),
      )
      .max(2),
  })
  .strict();
export const projectSchema = z
  .object({
    schema: z.literal(SCHEMA),
    id: uid,
    name: text(80).min(1),
    demo: z.boolean(),
    createdAt: z.string().datetime(),
    updatedAt: z.string().datetime(),
    theme: z.enum(["sakura", "moonlight", "sage"]),
    anniversary: z.array(anniversarySchema).max(32),
    letters: z.array(letterSchema).max(100),
    memories: z.array(memorySchema).max(300),
    album: z
      .object({
        coverId: uid.nullable(),
        photos: z.array(photoSchema).max(LIMITS.photos),
      })
      .strict(),
    story: z
      .object({
        quest: z
          .object({ title: text(80), objective: text(500), reward: text(80) })
          .strict(),
        chapters: z.array(chapterSchema).max(8),
      })
      .strict(),
    resources: z.array(resourceSchema).max(80),
  })
  .strict();
export type Project = z.infer<typeof projectSchema>;
export type Photo = Project["album"]["photos"][number];
export type Chapter = Project["story"]["chapters"][number];
export type Snapshot = { project: Project; media: Record<string, string> };
export function parseProject(input: unknown): Project {
  const result = projectSchema.safeParse(input);
  if (!result.success)
    throw new Error(
      result.error.issues
        .slice(0, 5)
        .map((i) => `${i.path.join(".")}: ${i.message}`)
        .join("\n"),
    );
  const p = result.data;
  for (const list of [
    p.anniversary,
    p.letters,
    p.memories,
    p.album.photos,
    p.story.chapters,
  ]) {
    if (new Set(list.map((x) => x.id)).size !== list.length)
      throw new Error("内容 ID 重复");
  }
  const ids = new Set(p.album.photos.map((x) => x.id)),
    chapters = new Set(p.story.chapters.map((x) => x.id));
  const resources = new Map(p.resources.map((x) => [x.path, x]));
  if (resources.size !== p.resources.length) throw new Error("资源路径重复");
  if (p.album.coverId && !ids.has(p.album.coverId))
    throw new Error("封面照片不存在");
  if (p.memories.some((m) => m.photoId && !ids.has(m.photoId)))
    throw new Error("回忆引用的照片不存在");
  const used = new Set<string>();
  for (const photo of p.album.photos) {
    if (
      photo.original === photo.preview ||
      photo.original !==
        `assets/${photo.id}/original.${photo.original.split(".").pop()}` ||
      photo.preview !== `assets/${photo.id}/preview.png`
    )
      throw new Error("照片资源路径与 ID 不一致");
    const orig = resources.get(photo.original),
      prev = resources.get(photo.preview);
    if (
      !orig ||
      !prev ||
      orig.role !== "original" ||
      prev.role !== "preview" ||
      prev.mime !== "image/png"
    )
      throw new Error("照片资源缺失或类型不符");
    const ext = {
      "image/png": "png",
      "image/jpeg": "jpg",
      "image/webp": "webp",
    }[orig.mime];
    if (
      !photo.original.endsWith(`.${ext}`) ||
      photo.estimatedRGB565Bytes !==
        photo.previewWidth * photo.previewHeight * 2
    )
      throw new Error("图片格式或占用估算不一致");
    used.add(photo.original);
    used.add(photo.preview);
  }
  if (used.size !== resources.size) throw new Error("资源清单包含未引用文件");
  if (p.resources.reduce((s, r) => s + r.bytes, 0) > LIMITS.totalMedia)
    throw new Error("图片总量超出 40 MiB");
  for (const c of p.story.chapters)
    for (const choice of c.choices)
      if (choice.nextId && !chapters.has(choice.nextId))
        throw new Error(`对话 ${c.id} 的选项引用了不存在的章节`);
  return p;
}
export function emptyProject(name = "我的小世界"): Project {
  const now = new Date().toISOString();
  return {
    schema: SCHEMA,
    id: crypto.randomUUID(),
    name,
    demo: false,
    createdAt: now,
    updatedAt: now,
    theme: "sakura",
    anniversary: [],
    letters: [],
    memories: [],
    album: { coverId: null, photos: [] },
    story: { quest: { title: "", objective: "", reward: "" }, chapters: [] },
    resources: [],
  };
}
export function demoProject(): Project {
  const p = emptyProject("樱花来信 · DEMO");
  p.demo = true;
  p.anniversary = [
    {
      id: crypto.randomUUID(),
      title: "小世界的第一天 · DEMO",
      date: "2024-10-25",
      description:
        "这里放下一个值得记住的日期。写上自己的标题，让每个普通日子都有回声。\n\n这是演示文字，请替换为你的真实内容。",
    },
    {
      id: crypto.randomUUID(),
      title: "下一次春日 · DEMO",
      date: "2027-03-20",
      description: "DEMO：为未来留一个温柔的约定。",
    },
  ];
  p.letters = [
    {
      id: crypto.randomUUID(),
      type: "today",
      date: localToday(),
      unlockDate: null,
      title: "有一封小信，想交给你 · DEMO",
      subtitle: "把心里的话，慢慢写下来",
      body: "DEMO · 这是一封等待你填写的信。\n\n可以写今天的小事，也可以写一句一直想说的话。没有固定的格式，只要是你自己的声音就很好。\n\n愿这个小小的世界，装得下你想珍藏的日常。",
    },
    {
      id: crypto.randomUUID(),
      type: "letter",
      date: "2027-01-01",
      unlockDate: "2027-01-01",
      title: "写给未来的一页 · DEMO",
      subtitle: "留到那一天，再打开",
      body: "DEMO：这里是一封未来的信。解锁日期目前只在 Studio 中模拟。",
    },
  ];
  p.memories = [
    {
      id: crypto.randomUUID(),
      date: "2024-10-25",
      title: "故事从这里开始 · DEMO",
      body: "这是时间线的示例。添加日期、照片与文字，收藏属于你的片段。",
      photoId: null,
      tags: ["开始", "DEMO"],
      visible: true,
    },
    {
      id: crypto.randomUUID(),
      date: "2026-03-20",
      title: "把春天收进一页 · DEMO",
      body: "DEMO：选择一张本地照片，让文字有一个温柔的落点。",
      photoId: null,
      tags: ["日常", "DEMO"],
      visible: true,
    },
  ];
  p.story = {
    quest: {
      title: "收集一页春天 · DEMO",
      objective: "DEMO：找到故事里的小信封。此处是剧情策划说明。",
      reward: "春日书签 · DEMO",
    },
    chapters: [
      {
        id: 1,
        title: "第一章 · 来信 DEMO",
        scene: "樱花书房 · DEMO",
        event: "DEMO：与桌上的信封互动。热点位置需要未来设备适配。",
        lines: [
          { speaker: "旁白 · DEMO", text: "桌上有一封还没有写完的信。" },
          { speaker: "讲述者 · DEMO", text: "要把今天的心情，也写进去吗？" },
        ],
        choices: [
          { text: "翻到下一页", nextId: 2 },
          { text: "留给下一次", nextId: 0 },
        ],
      },
      {
        id: 2,
        title: "第二章 · 留白 DEMO",
        scene: "樱花书房 · DEMO",
        event: "DEMO：阅读下一页。",
        lines: [{ speaker: "旁白 · DEMO", text: "新的一页，等着你亲手写下。" }],
        choices: [],
      },
    ],
  };
  return p;
}
export function moveItem<T>(items: T[], from: number, to: number): T[] {
  if (from < 0 || to < 0 || from >= items.length || to >= items.length)
    return items;
  const copy = [...items],
    [item] = copy.splice(from, 1);
  copy.splice(to, 0, item);
  return copy;
}
export function removePhoto(p: Project, id: string): Project {
  const photo = p.album.photos.find((x) => x.id === id);
  if (!photo) return p;
  const photos = p.album.photos.filter((x) => x.id !== id);
  return {
    ...p,
    album: {
      photos,
      coverId:
        p.album.coverId === id ? (photos[0]?.id ?? null) : p.album.coverId,
    },
    memories: p.memories.map((m) =>
      m.photoId === id ? { ...m, photoId: null } : m,
    ),
    resources: p.resources.filter(
      (r) => r.path !== photo.original && r.path !== photo.preview,
    ),
  };
}
