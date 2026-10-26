import { useRef, useState } from "react";
import {
  CalendarDays,
  Heart,
  Mail,
  ImagePlus,
  GripVertical,
  Star,
  EyeOff,
  Download,
  Upload,
  Play,
  RotateCcw,
  GitBranch,
  ArrowRight,
  Check,
  ShieldCheck,
  FolderArchive,
  BookOpen,
} from "lucide-react";
import type { Project, Snapshot, Chapter } from "../shared/model.ts";
import { textAdapterIssues } from "../shared/compatibility.ts";
import {
  LIMITS,
  moveItem,
  parseProject,
  removePhoto,
} from "../shared/model.ts";
import {
  daysBetween,
  localToday,
  nextAnniversary,
  validDate,
} from "../shared/date.ts";
import {
  Button,
  Field,
  Empty,
  Add,
  ItemActions,
  SearchBox,
} from "./components.tsx";
import { download } from "./api.ts";
import { imageUrl, preparePhoto, sizeLabel } from "./media.ts";
import type { Player } from "./Preview.tsx";
export type Change = (
  edit: (p: Project) => void,
  media?: Record<string, string>,
) => void;
type Props = {
  snapshot: Snapshot;
  selected: string | number | null;
  select: (id: string | number) => void;
  change: Change;
  report: (message: string) => void;
  confirmDelete: (name: string, action: () => void) => void;
};
const id = () => crypto.randomUUID();
const dateInputProps = { min: "1970-01-01", max: "2200-12-31" };
export function AnniversaryEditor({
  snapshot,
  selected,
  select,
  change,
  confirmDelete,
}: Props) {
  const p = snapshot.project,
    entry = p.anniversary.find((x) => x.id === selected) ?? p.anniversary[0],
    today = localToday();
  const add = () => {
    const key = id();
    change((p) => {
      p.anniversary.push({
        id: key,
        title: "新的纪念日",
        date: today,
        description: "",
      });
    });
    select(key);
  };
  const patch = (field: string, value: string) =>
    change((p) => {
      Object.assign(
        p.anniversary.find((x) => x.id === entry.id)!,
        { [field]: value },
      );
    });
  const days =
    entry && validDate(entry.date) ? daysBetween(entry.date, today) : null;
  return (
    <>
      <div className="editor-title">
        <div>
          <span className="eyebrow">DAYS TO REMEMBER</span>
          <h1>
            把重要的日子，
            <br />
            好好收藏。
          </h1>
          <p>从故事的第一天，到每个值得期待的明天。</p>
        </div>
        <div className="title-art">
          <CalendarDays size={42} strokeWidth={1} />
          <span>✿</span>
        </div>
      </div>
      <div className="section-row">
        <h2>
          我的纪念日{" "}
          <span>{p.anniversary.length.toString().padStart(2, "0")}</span>
        </h2>
        <Add onClick={add} disabled={p.anniversary.length >= 32}>
          添加日期
        </Add>
      </div>
      <div className="anniversary-list">
        {p.anniversary.map((a, i) => (
          <button
            key={a.id}
            className={`anniversary-tile ${a.id === entry?.id ? "selected" : ""}`}
            onClick={() => select(a.id)}
          >
            <span className="tile-icon">
              <Heart size={19} />
            </span>
            <span>
              <strong>{a.title || "未命名纪念日"}</strong>
              <small>
                {a.date} {i === 0 ? "· 主纪念日" : ""}
              </small>
            </span>
            <span className="tile-dot" />
          </button>
        ))}
      </div>
      {entry ? (
        <section className="edit-card">
          <header>
            <h3>
              <CalendarDays size={18} /> 编辑这一页
            </h3>
            <span className="tiny-label">本地自动保存</span>
          </header>
          <Field label="纪念日标题">
            <input
              aria-label="纪念日标题"
              maxLength={80}
              value={entry.title}
              onChange={(e) => patch("title", e.target.value)}
              placeholder="例如：故事开始的那一天"
            />
          </Field>
          <div className="form-grid">
            <Field label="选择日期">
              <input
                aria-label="纪念日日期"
                type="date"
                {...dateInputProps}
                value={entry.date}
                onChange={(e) => patch("date", e.target.value)}
              />
            </Field>
            <div className="day-stat">
              <span>
                {days !== null && days < 0 ? "距离这一天" : "已经一起走过"}
              </span>
              <strong>
                {days === null ? "—" : Math.abs(days)} <small>天</small>
              </strong>
            </div>
          </div>
          <Field
            label="写给这一天的话"
            hint={`${Array.from(entry.description).length} / 2000 字符`}
          >
            <textarea
              aria-label="纪念卡片说明"
              rows={5}
              maxLength={2000}
              value={entry.description}
              onChange={(e) => patch("description", e.target.value)}
              placeholder="一句话，一段回忆，都可以留在这里。"
            />
          </Field>
          <div className="card-footer">
            <small>
              {entry && validDate(entry.date)
                ? `下次周年日 ${nextAnniversary(entry.date, today)} · 闰日按 2 月 28 日纪念`
                : ""}
            </small>
            <ItemActions
              index={p.anniversary.indexOf(entry)}
              count={p.anniversary.length}
              onMove={(a, b) =>
                change((p) => {
                  p.anniversary = moveItem(p.anniversary, a, b);
                })
              }
              onDelete={() =>
                confirmDelete(entry.title, () =>
                  change((p) => {
                    p.anniversary = p.anniversary.filter(
                      (x) => x.id !== entry.id,
                    );
                  }),
                )
              }
              label="纪念日"
            />
          </div>
        </section>
      ) : (
        <Empty
          title="留一个日期给小世界"
          description="添加你的第一个纪念日，它会出现在右边的设备预览里。"
          action={<Add onClick={add}>添加纪念日</Add>}
        />
      )}
      <div className="gentle-note">
        <Heart size={16} />
        <span>日子按本地日历计算，开始当天为第 0 天。</span>
      </div>
    </>
  );
}
export function LettersEditor({
  snapshot,
  selected,
  select,
  change,
  confirmDelete,
}: Props) {
  const list = snapshot.project.letters,
    entry = list.find((x) => x.id === selected) ?? list[0];
  const add = () => {
    const key = id();
    change((p) => {
      p.letters.push({
        id: key,
        type: "letter",
        date: localToday(),
        unlockDate: null,
        title: "一封新的小信",
        subtitle: "",
        body: "",
      });
    });
    select(key);
  };
  const patch = (field: string, value: unknown) =>
    change((p) => {
      Object.assign(
        p.letters.find((x) => x.id === entry.id)!,
        { [field]: value },
      );
    });
  return (
    <>
      <ModuleTitle
        eyebrow="WORDS FROM THE HEART"
        title="有些话，想慢慢说。"
        description="给今天、给未来，也给心里特别的那个人。"
      />
      <div className="section-row">
        <h2>
          小信封 <span>{list.length}</span>
        </h2>
        <Add onClick={add} disabled={list.length >= 100}>
          写一封信
        </Add>
      </div>
      <div className="letter-tabs">
        {list.map((l) => (
          <button
            key={l.id}
            className={l.id === entry?.id ? "active" : ""}
            onClick={() => select(l.id)}
          >
            <Mail size={16} />
            <span>
              {l.title || "未命名信件"}
              <small>{l.date}</small>
            </span>
          </button>
        ))}
      </div>
      {entry ? (
        <section className="edit-card">
          <header>
            <h3>
              <Mail size={18} /> 打开这封信
            </h3>
            <small>{Array.from(entry.body).length} / 20000 字符</small>
          </header>
          <div className="form-grid">
            <Field label="信件类型">
              <select
                aria-label="信件类型"
                value={entry.type}
                onChange={(e) => patch("type", e.target.value)}
              >
                <option value="today">今日小信封</option>
                <option value="wish">祝福文字</option>
                <option value="letter">一封完整的信</option>
              </select>
            </Field>
            <Field label="信件日期">
              <input
                aria-label="信件日期"
                type="date"
                {...dateInputProps}
                value={entry.date}
                onChange={(e) => patch("date", e.target.value)}
              />
            </Field>
          </div>
          <Field label="信件标题">
            <input
              aria-label="信件标题"
              maxLength={80}
              value={entry.title}
              onChange={(e) => patch("title", e.target.value)}
            />
          </Field>
          <Field label="副标题">
            <input
              aria-label="信件副标题"
              maxLength={160}
              value={entry.subtitle}
              onChange={(e) => patch("subtitle", e.target.value)}
              placeholder="轻轻写一句开场白"
            />
          </Field>
          <Field label="正文">
            <textarea
              className="writing-area"
              aria-label="信件正文"
              maxLength={20000}
              rows={12}
              value={entry.body}
              onChange={(e) => patch("body", e.target.value)}
              placeholder="亲爱的，今天想和你说…"
            />
          </Field>
          <Field
            label="解锁日期（可选）"
            hint="仅作 Studio 内容规则预览；设备端尚未实现定时解锁。"
          >
            <input
              aria-label="解锁日期"
              type="date"
              {...dateInputProps}
              value={entry.unlockDate ?? ""}
              onChange={(e) => patch("unlockDate", e.target.value || null)}
            />
          </Field>
          <div className="card-footer">
            <small>文字只保存在这台电脑。</small>
            <ItemActions
              index={list.indexOf(entry)}
              count={list.length}
              onMove={(a, b) =>
                change((p) => {
                  p.letters = moveItem(p.letters, a, b);
                })
              }
              onDelete={() =>
                confirmDelete(entry.title, () =>
                  change((p) => {
                    p.letters = p.letters.filter((x) => x.id !== entry.id);
                  }),
                )
              }
              label="信件"
            />
          </div>
        </section>
      ) : (
        <Empty
          title="第一封信，从一句话开始"
          description="写下来的心意，会在小世界里慢慢被读到。"
          action={<Add onClick={add}>写一封信</Add>}
        />
      )}
    </>
  );
}
export function MemoriesEditor(props: Props) {
  const { snapshot, selected, select, change, report, confirmDelete } = props,
    [query, setQuery] = useState(""),
    fileRef = useRef<HTMLInputElement>(null),
    p = snapshot.project;
  const list = p.memories,
    filtered = list.filter((m) =>
      `${m.date} ${m.title} ${m.body} ${m.tags.join(" ")}`
        .toLocaleLowerCase()
        .includes(query.toLocaleLowerCase()),
    ),
    entry = list.find((x) => x.id === selected) ?? list[0];
  const add = () => {
    const key = id();
    change((p) => {
      p.memories.push({
        id: key,
        date: localToday(),
        title: "新的回忆",
        body: "",
        photoId: null,
        tags: [],
        visible: true,
      });
    });
    select(key);
  };
  const patch = (field: string, value: unknown) =>
    change((p) => {
      Object.assign(
        p.memories.find((x) => x.id === entry.id)!,
        { [field]: value },
      );
    });
  const importMemories = async (file?: File) => {
    if (!file) return;
    try {
      if (file.size > 4 * 1024 * 1024)
        throw new Error("时间线 JSON 上限为 4 MiB");
      const data = JSON.parse(await file.text());
      if (
        data.schema !== "littleworld-memories/1" ||
        !Array.isArray(data.memories)
      )
        throw new Error("不是有效的时间线文件");
      const next = parseProject({
        ...p,
        memories: [
          ...p.memories,
          ...data.memories.map((m: Record<string, unknown>) => ({
            ...m,
            id: id(),
            photoId: null,
          })),
        ],
      });
      change((p) => {
        p.memories = next.memories;
      });
      report("时间线已导入。照片请使用完整内容包导入。");
    } catch (e) {
      report((e as Error).message);
    }
  };
  return (
    <>
      <ModuleTitle
        eyebrow="A COLLECTION OF LITTLE MOMENTS"
        title="把回忆，连成一条线。"
        description="珍贵的从来不止大日子，还有那些细小的日常。"
      />
      <div className="section-row">
        <h2>
          回忆时间线 <span>{list.length}</span>
        </h2>
        <Add onClick={add} disabled={list.length >= 300}>
          记录回忆
        </Add>
      </div>
      <SearchBox value={query} onChange={setQuery} />
      <div className="text-toolbar">
        <button
          onClick={() =>
            download(
              JSON.stringify(
                {
                  schema: "littleworld-memories/1",
                  memories: list.map((m) => ({ ...m, photoId: null })),
                },
                null,
                2,
              ),
              "LittleWorld-memories.json",
              "application/json",
            )
          }
        >
          <Download size={14} />
          导出时间线
        </button>
        <button onClick={() => fileRef.current?.click()}>
          <Upload size={14} />
          导入时间线
        </button>
        <input
          ref={fileRef}
          hidden
          type="file"
          accept=".json"
          aria-label="导入时间线文件"
          onChange={(e) => {
            void importMemories(e.target.files?.[0]);
            e.target.value = "";
          }}
        />
      </div>
      <div className="timeline-list">
        {filtered.map((m) => (
          <button
            key={m.id}
            className={`timeline-row ${m.id === entry?.id ? "active" : ""}`}
            onClick={() => select(m.id)}
          >
            <span className="timeline-point" />
            <small>{m.date}</small>
            <strong>{m.title || "未命名回忆"}</strong>
            {!m.visible && <EyeOff size={14} />}
            <span>{m.tags.join(" · ")}</span>
          </button>
        ))}
        {query && !filtered.length && (
          <p className="muted">没有找到相关回忆。</p>
        )}
      </div>
      {entry ? (
        <section className="edit-card">
          <header>
            <h3>
              <BookOpen size={18} /> 回忆的这一页
            </h3>
            <label className="switch">
              <input
                aria-label="公开显示回忆"
                type="checkbox"
                checked={entry.visible}
                onChange={(e) => patch("visible", e.target.checked)}
              />
              <span>公开显示</span>
            </label>
          </header>
          <div className="form-grid">
            <Field label="回忆标题">
              <input
                aria-label="回忆标题"
                value={entry.title}
                maxLength={80}
                onChange={(e) => patch("title", e.target.value)}
              />
            </Field>
            <Field label="回忆日期">
              <input
                aria-label="回忆日期"
                type="date"
                {...dateInputProps}
                value={entry.date}
                onChange={(e) => patch("date", e.target.value)}
              />
            </Field>
          </div>
          <Field label="回忆正文">
            <textarea
              aria-label="回忆正文"
              rows={6}
              maxLength={10000}
              value={entry.body}
              onChange={(e) => patch("body", e.target.value)}
            />
          </Field>
          <div className="form-grid">
            <Field label="关联照片">
              <select
                aria-label="回忆照片"
                value={entry.photoId ?? ""}
                onChange={(e) => patch("photoId", e.target.value || null)}
              >
                <option value="">不使用照片</option>
                {p.album.photos.map((a) => (
                  <option key={a.id} value={a.id}>
                    {a.title}
                  </option>
                ))}
              </select>
            </Field>
            <Field label="标签" hint="用逗号分隔，最多 12 个，每个 24 字符。">
              <input
                aria-label="回忆标签"
                value={entry.tags.join(",")}
                onChange={(e) =>
                  patch(
                    "tags",
                    e.target.value
                      .split(/[,，]/)
                      .slice(0, 12)
                      .map((s) => s.trim().slice(0, 24)),
                  )
                }
                maxLength={300}
              />
            </Field>
          </div>
          <div className="card-footer">
            <small>
              设备文本接口：最多 32 条公开回忆，标题 128 / 正文 4096 UTF-8
              字节。超限内容可本地存档，C 转换将拒绝。
            </small>
            <ItemActions
              index={list.indexOf(entry)}
              count={list.length}
              onMove={(a, b) =>
                change((p) => {
                  p.memories = moveItem(p.memories, a, b);
                })
              }
              onDelete={() =>
                confirmDelete(entry.title, () =>
                  change((p) => {
                    p.memories = p.memories.filter((x) => x.id !== entry.id);
                  }),
                )
              }
              label="回忆"
            />
          </div>
        </section>
      ) : (
        <Empty
          title="把第一个片段留下来"
          description="添加文字，或者从相册关联一张照片。"
          action={<Add onClick={add}>添加回忆</Add>}
        />
      )}
    </>
  );
}
export function AlbumEditor({
  snapshot,
  selected,
  select,
  change,
  report,
  confirmDelete,
}: Props) {
  const fileRef = useRef<HTMLInputElement>(null),
    [busy, setBusy] = useState(false),
    [dragged, setDragged] = useState<number | null>(null),
    p = snapshot.project,
    entry = p.album.photos.find((x) => x.id === selected) ?? p.album.photos[0];
  const addFiles = async (files: File[]) => {
    if (busy) return;
    if (files.length + p.album.photos.length > LIMITS.photos)
      return report("每个项目最多 40 张照片");
    setBusy(true);
    try {
      const prepared: Awaited<ReturnType<typeof preparePhoto>>[] = [];
      for (const file of files) prepared.push(await preparePhoto(file));
      const bytes =
        prepared.flatMap((x) => x.resources).reduce((s, r) => s + r.bytes, 0) +
        p.resources.reduce((s, r) => s + r.bytes, 0);
      if (bytes > LIMITS.totalMedia)
        throw new Error("原图与预览图合计不能超过 40 MiB");
      const media = Object.assign({}, ...prepared.map((x) => x.media));
      change((p) => {
        for (const item of prepared) {
          p.album.photos.push(item.photo);
          p.resources.push(...item.resources);
        }
        if (!p.album.coverId) p.album.coverId = p.album.photos[0]?.id ?? null;
      }, media);
      if (prepared.length) select(prepared.at(-1)!.photo.id);
      report(`已添加 ${prepared.length} 张照片，并在本地生成预览。`);
    } catch (e) {
      report((e as Error).message);
    } finally {
      setBusy(false);
    }
  };
  return (
    <>
      <ModuleTitle
        eyebrow="A PLACE FOR YOUR PHOTOGRAPHS"
        title="瞬间，也有自己的家。"
        description="原图留在本机，预览轻盈清晰。每一张都由你排序。"
      />
      <div className="section-row">
        <h2>
          我们的相册 <span>{p.album.photos.length} / 40</span>
        </h2>
        <Button
          kind="soft"
          disabled={busy}
          onClick={() => fileRef.current?.click()}
        >
          <ImagePlus size={16} />
          {busy ? "正在处理照片…" : "选择本地照片"}
        </Button>
      </div>
      <input
        hidden
        ref={fileRef}
        type="file"
        accept="image/png,image/jpeg,image/webp"
        multiple
        aria-label="选择本地照片文件"
        onChange={(e) => {
          void addFiles(Array.from(e.target.files ?? []));
          e.target.value = "";
        }}
      />
      <div
        className="photo-drop"
        onDragOver={(e) => {
          e.preventDefault();
        }}
        onDrop={(e) => {
          e.preventDefault();
          if (e.dataTransfer.files.length)
            void addFiles(Array.from(e.dataTransfer.files));
        }}
      >
        <ImagePlus size={24} strokeWidth={1.3} />
        <span>把照片拖到这里，或点击上方选择</span>
        <small>PNG / JPEG / WebP · 每张 ≤ 10 MiB · 总量 ≤ 40 MiB</small>
      </div>
      <div className="photo-grid">
        {p.album.photos.map((photo, index) => (
          <div
            key={photo.id}
            draggable
            onDragStart={(e) => {
              setDragged(index);
              e.dataTransfer.setData("text/plain", String(index));
              e.dataTransfer.effectAllowed = "move";
            }}
            onDragEnd={() => setDragged(null)}
            onDragOver={(e) => e.preventDefault()}
            onDrop={(e) => {
              e.preventDefault();
              e.stopPropagation();
              if (dragged !== null)
                change((p) => {
                  p.album.photos = moveItem(p.album.photos, dragged, index);
                });
              setDragged(null);
            }}
            className={`photo-card ${photo.id === entry?.id ? "selected" : ""}`}
          >
            <button className="photo-select" onClick={() => select(photo.id)}>
              <img src={imageUrl(snapshot, photo.preview)} alt={photo.title} />
              <span>
                <GripVertical size={14} />
                {photo.title || "未命名照片"}
              </span>
            </button>
            {photo.id === p.album.coverId && (
              <span className="cover-badge">
                <Star size={11} />
                封面
              </span>
            )}
          </div>
        ))}
      </div>
      {entry ? (
        <section className="edit-card">
          <header>
            <h3>照片信息</h3>
            <Button
              kind="small"
              onClick={() =>
                change((p) => {
                  p.album.coverId = entry.id;
                })
              }
            >
              <Star size={14} />
              {p.album.coverId === entry.id ? "已设为封面" : "设为封面"}
            </Button>
          </header>
          <Field label="照片标题">
            <input
              aria-label="照片标题"
              value={entry.title}
              maxLength={80}
              onChange={(e) =>
                change((p) => {
                  p.album.photos.find((x) => x.id === entry.id)!.title =
                    e.target.value;
                })
              }
            />
          </Field>
          <div className="photo-specs">
            <div>
              <span>原始照片</span>
              <strong>
                {entry.width} × {entry.height}
              </strong>
              <small>
                {sizeLabel(
                  p.resources.find((r) => r.path === entry.original)!.bytes,
                )}{" "}
                · 保留原文件
              </small>
            </div>
            <div>
              <span>设备预览版本</span>
              <strong>
                {entry.previewWidth} × {entry.previewHeight}
              </strong>
              <small>PNG · 浏览器预览</small>
            </div>
            <div>
              <span>待转换设备资源</span>
              <strong>{sizeLabel(entry.estimatedRGB565Bytes)}</strong>
              <small>RGB565 裸像素估算，尚未生成</small>
            </div>
          </div>
          <div className="card-footer">
            <small>拖动缩略图或使用箭头排序。</small>
            <ItemActions
              index={p.album.photos.indexOf(entry)}
              count={p.album.photos.length}
              onMove={(a, b) =>
                change((p) => {
                  p.album.photos = moveItem(p.album.photos, a, b);
                })
              }
              onDelete={() =>
                confirmDelete(entry.title, () =>
                  change((p) => {
                    Object.assign(p, removePhoto(p, entry.id));
                  }),
                )
              }
              label="照片"
            />
          </div>
        </section>
      ) : (
        <Empty
          title="等一张你喜欢的照片"
          description="选择后会生成不超过 1024 × 600 的预览图。"
        />
      )}
      <div className="notice">
        <ShieldCheck size={18} />
        <p>
          电脑可预览图片，尚未证明 ESP32-P4
          可直接加载。图片解码、颜色格式和设备资源加载器仍需后续集成。
        </p>
      </div>
    </>
  );
}
export function StoryEditor({
  snapshot,
  selected,
  select,
  change,
  confirmDelete,
  player,
  onPlayer,
}: Props & { player: Player; onPlayer: (player: Player) => void }) {
  const story = snapshot.project.story,
    entry = story.chapters.find((x) => x.id === selected) ?? story.chapters[0];
  const add = () => {
    const next = Array.from({ length: 8 }, (_, i) => i + 1).find(
      (id) => !story.chapters.some((c) => c.id === id),
    );
    if (!next) return;
    change((p) => {
      p.story.chapters.push({
        id: next,
        title: `第 ${next} 章`,
        scene: "",
        event: "",
        lines: [{ speaker: "旁白", text: "" }],
        choices: [],
      });
    });
    select(next);
    onPlayer({ playing: false, chapterId: next, line: 0, ended: false });
  };
  const patch = (edit: (ch: Chapter) => void) =>
    change((p) => {
      edit(p.story.chapters.find((x) => x.id === entry.id)!);
      onPlayer({ ...player, playing: false, line: 0, ended: false });
    });
  return (
    <>
      <ModuleTitle
        eyebrow="WRITE A LITTLE ADVENTURE"
        title="我们的故事，由你续写。"
        description="一句对话，一个选择。让小世界里的人物有话可说。"
      />
      <div className="section-row">
        <h2>
          章节与对话 <span>{story.chapters.length} / 8</span>
        </h2>
        <Add onClick={add} disabled={story.chapters.length >= 8}>
          添加章节
        </Add>
      </div>
      <div className="chapter-list">
        {story.chapters.map((c, i) => (
          <button
            key={c.id}
            className={c.id === entry?.id ? "active" : ""}
            onClick={() => {
              select(c.id);
              onPlayer({
                chapterId: c.id,
                line: 0,
                playing: false,
                ended: false,
              });
            }}
          >
            <span>{String(i + 1).padStart(2, "0")}</span>
            <strong>{c.title || "未命名章节"}</strong>
            <small>{c.lines.length} 行</small>
          </button>
        ))}
      </div>
      {entry ? (
        <>
          <section className="edit-card">
            <header>
              <h3>
                <BookOpen size={18} /> 章节 #{entry.id}
              </h3>
              <Button
                kind="soft small"
                onClick={() =>
                  onPlayer({
                    chapterId: entry.id,
                    line: 0,
                    playing: true,
                    ended: false,
                  })
                }
              >
                <Play size={14} />
                {player.playing ? "重新播放" : "播放剧情"}
              </Button>
            </header>
            <div className="form-grid">
              <Field label="章节标题">
                <input
                  aria-label="章节标题"
                  value={entry.title}
                  maxLength={80}
                  onChange={(e) =>
                    patch((c) => {
                      c.title = e.target.value;
                    })
                  }
                />
              </Field>
              <Field label="场景名称">
                <input
                  aria-label="场景名称"
                  value={entry.scene}
                  maxLength={80}
                  onChange={(e) =>
                    patch((c) => {
                      c.scene = e.target.value;
                    })
                  }
                />
              </Field>
            </div>
            <Field label="互动事件说明" hint="策划备注，尚未映射为房间热点。">
              <textarea
                aria-label="互动事件说明"
                rows={2}
                value={entry.event}
                maxLength={500}
                onChange={(e) =>
                  patch((c) => {
                    c.event = e.target.value;
                  })
                }
              />
            </Field>
            <div className="section-row">
              <h3>
                对话 <span>{entry.lines.length} / 8</span>
              </h3>
              <Add
                disabled={entry.lines.length >= 8}
                onClick={() =>
                  patch((c) => {
                    c.lines.push({ speaker: "", text: "" });
                  })
                }
              >
                添加台词
              </Add>
            </div>
            <div className="dialogue-editor">
              {entry.lines.map((line, i) => (
                <div className="dialogue-line" key={i}>
                  <span className="line-number">
                    {String(i + 1).padStart(2, "0")}
                  </span>
                  <div>
                    <input
                      aria-label={`第${i + 1}行说话者`}
                      placeholder="说话者"
                      maxLength={40}
                      value={line.speaker}
                      onChange={(e) =>
                        patch((c) => {
                          c.lines[i].speaker = e.target.value;
                        })
                      }
                    />
                    <textarea
                      aria-label={`第${i + 1}行对话`}
                      rows={2}
                      maxLength={1000}
                      value={line.text}
                      placeholder="在这一刻，想说些什么？"
                      onChange={(e) =>
                        patch((c) => {
                          c.lines[i].text = e.target.value;
                        })
                      }
                    />
                  </div>
                  <ItemActions
                    index={i}
                    count={entry.lines.length}
                    onMove={(a, b) =>
                      patch((c) => {
                        c.lines = moveItem(c.lines, a, b);
                      })
                    }
                    onDelete={() => {
                      if (entry.lines.length > 1)
                        patch((c) => {
                          c.lines.splice(i, 1);
                        });
                    }}
                    label={`第${i + 1}行台词`}
                  />
                </div>
              ))}
            </div>
            <div className="section-row">
              <h3>
                结尾选项 <span>{entry.choices.length} / 2</span>
              </h3>
              <Add
                disabled={entry.choices.length >= 2}
                onClick={() =>
                  patch((c) => {
                    c.choices.push({ text: "新的选择", nextId: 0 });
                  })
                }
              >
                添加选项
              </Add>
            </div>
            {entry.choices.map((choice, i) => (
              <div className="choice-editor" key={i}>
                <Field label={`选项 ${i + 1}`}>
                  <input
                    aria-label={`选项${i + 1}文字`}
                    maxLength={80}
                    value={choice.text}
                    onChange={(e) =>
                      patch((c) => {
                        c.choices[i].text = e.target.value;
                      })
                    }
                  />
                </Field>
                <Field label="接下来">
                  <select
                    aria-label={`选项${i + 1}目标`}
                    value={choice.nextId}
                    onChange={(e) =>
                      patch((c) => {
                        c.choices[i].nextId = Number(e.target.value);
                      })
                    }
                  >
                    <option value={0}>结束对话</option>
                    {story.chapters.map((c) => (
                      <option value={c.id} key={c.id}>
                        #{c.id} {c.title}
                      </option>
                    ))}
                  </select>
                </Field>
                <Button
                  kind="icon danger"
                  aria-label={`删除选项${i + 1}`}
                  onClick={() =>
                    patch((c) => {
                      c.choices.splice(i, 1);
                    })
                  }
                >
                  ×
                </Button>
              </div>
            ))}
            <div className="card-footer">
              <small>选项在本段最后一行之后出现。</small>
              <ItemActions
                index={story.chapters.indexOf(entry)}
                count={story.chapters.length}
                onMove={(a, b) =>
                  change((p) => {
                    p.story.chapters = moveItem(p.story.chapters, a, b);
                  })
                }
                onDelete={() =>
                  confirmDelete(entry.title, () => {
                    change((p) => {
                      p.story.chapters = p.story.chapters
                        .filter((c) => c.id !== entry.id)
                        .map((c) => ({
                          ...c,
                          choices: c.choices.map((o) =>
                            o.nextId === entry.id ? { ...o, nextId: 0 } : o,
                          ),
                        }));
                    });
                    onPlayer({ ...player, playing: false });
                  })
                }
                label="章节"
              />
            </div>
          </section>
          <section className="edit-card flow-card">
            <header>
              <h3>
                <GitBranch size={17} /> 对话流程
              </h3>
              <small>实线表示可用的 next_id 跳转</small>
            </header>
            <div className="flow">
              <span className="flow-node">
                #{entry.id} · {entry.lines.length} 行对话
              </span>
              <ArrowRight size={18} />
              <div>
                {entry.choices.length ? (
                  entry.choices.map((c, i) => (
                    <div className="flow-target" key={i}>
                      <span>{c.text}</span>
                      <ArrowRight size={14} />
                      <b>{c.nextId ? `章节 #${c.nextId}` : "结束"}</b>
                    </div>
                  ))
                ) : (
                  <span className="flow-target">结束对话</span>
                )}
              </div>
            </div>
          </section>
        </>
      ) : (
        <Empty
          title="让故事从第一句话开始"
          description="每个章节映射一段 lw_rpg 对话，可在右侧逐句播放。"
          action={<Add onClick={add}>添加章节</Add>}
        />
      )}
      <section className="edit-card">
        <header>
          <h3>故事任务</h3>
          <span className="tiny-label">策划元数据</span>
        </header>
        <Field label="任务名称">
          <input
            aria-label="任务名称"
            maxLength={80}
            value={story.quest.title}
            onChange={(e) =>
              change((p) => {
                p.story.quest.title = e.target.value;
              })
            }
          />
        </Field>
        <Field label="任务目标">
          <textarea
            aria-label="任务目标"
            rows={2}
            maxLength={500}
            value={story.quest.objective}
            onChange={(e) =>
              change((p) => {
                p.story.quest.objective = e.target.value;
              })
            }
          />
        </Field>
        <Field label="奖励名称">
          <input
            aria-label="奖励名称"
            maxLength={80}
            value={story.quest.reward}
            onChange={(e) =>
              change((p) => {
                p.story.quest.reward = e.target.value;
              })
            }
          />
        </Field>
        <small>
          核心只有一项任务。房间、热点坐标、物品与奖励 ID
          需要设备适配；这里不会生成新的运行时能力。
        </small>
      </section>
    </>
  );
}
export function PackageEditor({
  snapshot,
  onExport,
  onProjectFile,
  onBackup,
  onImport,
}: {
  snapshot: Snapshot;
  onExport: () => void;
  onProjectFile: () => void;
  onBackup: () => void;
  onImport: () => void;
}) {
  const p = snapshot.project,
    bytes = p.resources.reduce((s, r) => s + r.bytes, 0);
  return (
    <>
      <ModuleTitle
        eyebrow="READY FOR THE NEXT CHAPTER"
        title="把小世界，装订成册。"
        description="内容和照片一起带走，下次打开，故事还在原处。"
      />
      <section className="export-hero">
        <FolderArchive size={42} strokeWidth={1.2} />
        <h2>{p.name}</h2>
        <span>littleworld-content/1</span>
        <p>UTF-8 · 独立媒体文件 · SHA-256 完整性校验</p>
        <Button kind="primary" onClick={onExport}>
          <Download size={16} />
          导出内容包 .lwpack
        </Button>
      </section>
      <div className="package-stats">
        {[
          [p.anniversary.length, "纪念日"],
          [p.letters.length, "封信"],
          [p.memories.length, "回忆"],
          [p.album.photos.length, "照片"],
          [p.story.chapters.length, "章节"],
        ].map(([n, l]) => (
          <div key={l}>
            <strong>{n}</strong>
            <span>{l}</span>
          </div>
        ))}
      </div>
      <section className="edit-card">
        <header>
          <h3>
            <ShieldCheck size={18} /> 本地文件与恢复
          </h3>
          <small>媒体 {sizeLabel(bytes)} / 40 MiB</small>
        </header>
        <div className="action-row">
          <Button onClick={onProjectFile}>
            <Download size={16} />
            下载项目文件
          </Button>
          <Button onClick={onImport}>
            <Upload size={16} />
            导入内容包
          </Button>
          <Button onClick={onBackup}>
            <RotateCcw size={16} />
            备份与恢复
          </Button>
        </div>
        <p className="muted">
          项目文件 .lwstudio 与内容包 .lwpack
          使用同一版本化容器，两者均可重新导入。导入始终创建副本。
        </p>
      </section>
      <section className="edit-card compatibility">
        {textAdapterIssues(p).length > 0 && (
          <div className="notice">
            <p>{textAdapterIssues(p).join(" ")}</p>
          </div>
        )}
        <header>
          <h3>与 Little World 的连接</h3>
          <span className="tiny-label">准确的兼容边界</span>
        </header>
        <p>
          <Check size={16} /> 纪念日期可映射到 lw_state_t.anniversary
        </p>
        <p>
          <Check size={16} /> 符合字节及数量限制的公开回忆可转换为 lw_memory_t[]
        </p>
        <p>
          <Check size={16} /> 台词与选项符合 lw_rpg_dialogue_t 的数量限制
        </p>
        <div className="notice">
          <p>
            当前固件没有此内容包的读取器。信件解锁规则、图片加载、房间互动与任务配置尚需适配。此包不能直接烧写或宣称设备已支持。
          </p>
        </div>
        <small>
          CLI 可离线验证包，并导出供开发者审查的纯文本 C
          适配头文件。不会修改固件。
        </small>
      </section>
    </>
  );
}
function ModuleTitle({
  eyebrow,
  title,
  description,
}: {
  eyebrow: string;
  title: string;
  description: string;
}) {
  return (
    <div className="editor-title compact">
      <div>
        <span className="eyebrow">{eyebrow}</span>
        <h1>{title}</h1>
        <p>{description}</p>
      </div>
    </div>
  );
}
