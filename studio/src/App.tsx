import { useEffect, useRef, useState } from "react";
import {
  BookOpen,
  CalendarDays,
  Mail,
  Images,
  Route,
  Heart,
  FolderArchive,
  ChevronDown,
  ChevronRight,
  Plus,
  FolderOpen,
  Save,
  Download,
  ShieldCheck,
  Check,
  LoaderCircle,
  Sun,
  Moon,
  Leaf,
  Maximize2,
  Undo2,
  Redo2,
  AlertCircle,
  RotateCcw,
  Upload,
  ExternalLink,
} from "lucide-react";
import type { Project, Snapshot } from "../shared/model.ts";
import { parseProject } from "../shared/model.ts";
import { localToday } from "../shared/date.ts";
import { api, connect, exportPack, importPack } from "./api.ts";
import { Button, Modal, Field, Empty } from "./components.tsx";
import { Preview } from "./Preview.tsx";
import type { Module, Player } from "./Preview.tsx";
import {
  AnniversaryEditor,
  LettersEditor,
  MemoriesEditor,
  AlbumEditor,
  StoryEditor,
  PackageEditor,
} from "./editors.tsx";
import type { Change } from "./editors.tsx";
import { sizeLabel } from "./media.ts";
const nav = [
  {
    id: "anniversary",
    label: "纪念日",
    en: "Our special days",
    icon: CalendarDays,
  },
  { id: "letters", label: "小信封", en: "Letters to you", icon: Mail },
  { id: "memories", label: "回忆时间线", en: "Little memories", icon: Route },
  { id: "album", label: "相册", en: "Treasured moments", icon: Images },
  { id: "story", label: "我们的故事", en: "A story we write", icon: BookOpen },
  {
    id: "package",
    label: "内容与导出",
    en: "Pack your world",
    icon: FolderArchive,
  },
] as const;
type ProjectInfo = {
  id: string;
  name: string;
  updatedAt: string;
  demo: boolean;
  corrupt?: boolean;
};
export default function App() {
  const [data, setData] = useState<Snapshot | null>(null),
    [module, setModule] = useState<Module>("anniversary"),
    [selection, setSelection] = useState<
      Partial<Record<Module, string | number>>
    >({}),
    [status, setStatus] = useState("loading"),
    [saveError, setSaveError] = useState(""),
    [toast, setToast] = useState(""),
    [fatal, setFatal] = useState("");
  const [dialog, setDialog] = useState<
      "library" | "new" | "backups" | "preview" | null
    >(null),
    [projects, setProjects] = useState<ProjectInfo[]>([]),
    [backups, setBackups] = useState<string[]>([]),
    [name, setName] = useState("我的小世界"),
    [deleteAction, setDeleteAction] = useState<{
      name: string;
      action: () => void;
    } | null>(null),
    [busy, setBusy] = useState(false),
    [previewDate, setPreviewDate] = useState(localToday()),
    [player, setPlayer] = useState<Player>({
      chapterId: 1,
      line: 0,
      playing: false,
      ended: false,
    });
  const [history, setHistory] = useState<Snapshot[]>([]),
    [future, setFuture] = useState<Snapshot[]>([]);
  const latest = useRef<{
      snapshot: Snapshot | null;
      revision: number;
      saved: number;
    }>({ snapshot: null, revision: 0, saved: 0 }),
    diskStamp = useRef(new Map<string, string>()),
    queue = useRef<Promise<unknown>>(Promise.resolve()),
    input = useRef<HTMLInputElement>(null),
    toastTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const report = (message: string) => {
    setToast(message);
    if (toastTimer.current) clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(""), 6000);
  };
  const install = (snapshot: Snapshot) => {
    diskStamp.current.set(snapshot.project.id, snapshot.project.updatedAt);
    latest.current = { snapshot, revision: 0, saved: 0 };
    setData(snapshot);
    setStatus("saved");
    setSaveError("");
    setSelection({});
    setHistory([]);
    setFuture([]);
    setPlayer({ chapterId: 1, line: 0, playing: false, ended: false });
  };
  const apply = (snapshot: Snapshot) => {
    snapshot = {
      ...snapshot,
      project: { ...snapshot.project, updatedAt: new Date().toISOString() },
    };
    latest.current.snapshot = snapshot;
    latest.current.revision++;
    setData(snapshot);
    setStatus("dirty");
    setSaveError("");
  };
  const change: Change = (edit, media) => {
    const previous = latest.current.snapshot;
    if (!previous) return;
    if (previous.project.id !== data?.project.id) {
      report("项目已切换，请在当前项目重新选择照片。");
      return;
    }
    const project = structuredClone(previous.project);
    edit(project);
    const available = { ...previous.media, ...media },
      trimmed = Object.fromEntries(
        project.resources.map((r) => [r.path, available[r.path]]),
      );
    setHistory((h) => [...h.slice(-29), previous]);
    setFuture([]);
    apply({ project, media: trimmed });
  };
  const undo = () => {
    if (!history.length || !data) return;
    const prev = history.at(-1)!;
    setFuture((f) => [...f, data]);
    setHistory((h) => h.slice(0, -1));
    apply(prev);
  };
  const redo = () => {
    if (!future.length || !data) return;
    setHistory((h) => [...h, data]);
    setFuture((f) => f.slice(0, -1));
    apply(future.at(-1)!);
  };
  async function persist() {
    const current = latest.current,
      snapshot = current.snapshot,
      revision = current.revision;
    if (!snapshot || revision === current.saved) return;
    try {
      parseProject(snapshot.project);
    } catch (e) {
      setSaveError((e as Error).message);
      setStatus("error");
      throw e;
    }
    setStatus("saving");
    const job = queue.current
      .catch(() => {})
      .then(async () => {
        await api("save", {
          snapshot,
          expectedUpdatedAt: diskStamp.current.get(snapshot.project.id),
        });
        diskStamp.current.set(snapshot.project.id, snapshot.project.updatedAt);
      });
    queue.current = job;
    try {
      await job;
      if (latest.current.snapshot?.project.id === snapshot.project.id) {
        latest.current.saved = Math.max(latest.current.saved, revision);
        setStatus(
          latest.current.saved === latest.current.revision ? "saved" : "dirty",
        );
        setSaveError("");
      }
    } catch (e) {
      setSaveError((e as Error).message);
      setStatus("error");
      throw e;
    }
  }
  const safely = async (action: () => Promise<void>) => {
    if (busy) return;
    setBusy(true);
    try {
      await action();
    } catch (e) {
      report((e as Error).message);
    } finally {
      setBusy(false);
    }
  };
  const withSaved = async (action: () => Promise<void>) => {
    await persist();
    await action();
  };
  useEffect(() => {
    void (async () => {
      try {
        await connect();
        const list = await api<ProjectInfo[]>("projects");
        setProjects(list);
        const first = list.find((x) => !x.corrupt);
        install(
          first
            ? await api<Snapshot>(`project?id=${first.id}`)
            : await api<Snapshot>("new", { demo: true }),
        );
      } catch (e) {
        setFatal((e as Error).message);
      }
    })();
    return () => {
      if (toastTimer.current) clearTimeout(toastTimer.current);
    };
  }, []);
  useEffect(() => {
    if (!data || latest.current.revision === latest.current.saved) return;
    const timer = setTimeout(() => {
      void persist().catch(() => {});
    }, 900);
    return () => clearTimeout(timer);
  }, [data]);
  useEffect(() => {
    const unload = (e: BeforeUnloadEvent) => {
      if (latest.current.revision !== latest.current.saved) {
        e.preventDefault();
        e.returnValue = "";
      }
    };
    window.addEventListener("beforeunload", unload);
    return () => window.removeEventListener("beforeunload", unload);
  }, []);
  useEffect(() => {
    const keyboard = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
        e.preventDefault();
        void persist()
          .then(() => report("项目已保存到本机"))
          .catch((e) => report(e.message));
      }
      if (e.key === "Escape") {
        setDialog(null);
        setDeleteAction(null);
      }
    };
    window.addEventListener("keydown", keyboard);
    return () => window.removeEventListener("keydown", keyboard);
  });
  useEffect(() => {
    const timer = setInterval(() => {
      setPreviewDate((d) =>
        d === localToday(new Date(Date.now() - 60000)) ? localToday() : d,
      );
    }, 60000);
    return () => clearInterval(timer);
  }, []);
  const library = () =>
    void safely(() =>
      withSaved(async () => {
        setProjects(await api<ProjectInfo[]>("projects"));
        setDialog("library");
      }),
    );
  const showBackups = (projectId?: string) =>
    void safely(async () => {
      const key = projectId ?? data?.project.id;
      if (!key) return;
      setBackupProjectId(key);
      setBackups(await api<string[]>(`backups?id=${key}`));
      setDialog("backups");
    });
  const [backupProjectId, setBackupProjectId] = useState("");
  const exportContent = (ext = "lwpack") =>
    void safely(() =>
      (async () => {
        if (latest.current.snapshot)
          await exportPack(latest.current.snapshot, ext);
        report("文件已导出到浏览器下载位置。");
      })(),
    );
  const selected = selection[module] ?? null;
  const select = (id: string | number) =>
    setSelection((s) => ({ ...s, [module]: id }));
  if (fatal)
    return (
      <main className="boot">
        <BookOpen size={44} />
        <h1>Studio 暂时无法打开</h1>
        <p role="alert">{fatal}</p>
        <Button onClick={() => location.reload()}>重新连接本机服务</Button>
        <small>请保留启动窗口，并检查 4317 端口。</small>
      </main>
    );
  if (!data)
    return (
      <main className="boot">
        <LoaderCircle className="spin" size={36} />
        <h1>正在打开你的小世界…</h1>
        <p>所有内容只在这台电脑上。</p>
      </main>
    );
  const p = data.project,
    props = {
      snapshot: data,
      selected,
      select,
      change,
      report,
      confirmDelete: (name: string, action: () => void) =>
        setDeleteAction({ name, action }),
    };
  return (
    <div className={`app theme-${p.theme}`}>
      <aside className="sidebar">
        <div className="brand">
          <span className="brand-icon">
            <BookOpen size={24} strokeWidth={1.4} />
          </span>
          <span>
            Little World<small>STUDIO</small>
          </span>
          <span className="brand-petal">✿</span>
        </div>
        <button className="project-switch" onClick={library}>
          <span className="project-monogram">{p.name.slice(0, 1)}</span>
          <span>
            <small>当前项目</small>
            <strong>{p.name}</strong>
          </span>
          <ChevronDown size={15} />
        </button>
        <div className="nav-caption">创作你的小世界</div>
        <nav aria-label="内容模块">
          {nav.map((n) => (
            <button
              key={n.id}
              aria-label={n.label}
              onClick={() => {
                setModule(n.id);
                setPlayer((v) => ({ ...v, playing: false }));
              }}
              className={module === n.id ? "active" : ""}
              aria-current={module === n.id ? "page" : undefined}
            >
              <n.icon size={19} strokeWidth={1.6} />
              <span>
                {n.label}
                <small>{n.en}</small>
              </span>
              {module === n.id && <span className="nav-active-dot" />}
            </button>
          ))}
        </nav>
        <div className="sidebar-bottom">
          <div className="local-card">
            <ShieldCheck size={20} />
            <div>
              <strong>只属于你的小世界</strong>
              <p>
                本地保存，安心创作
                <br />
                无需账号，也无需云端
              </p>
            </div>
          </div>
          <span className="version">
            LITTLE WORLD STUDIO <b>v1.0</b>
          </span>
        </div>
      </aside>
      <div className="workspace">
        <header className="topbar">
          <div className="breadcrumb">
            工作室 <ChevronRight size={13} />
            <span>{nav.find((n) => n.id === module)?.label}</span>
            {p.demo && <span className="demo-badge">DEMO 演示项目</span>}
          </div>
          <div className="top-actions">
            <span className={`save-state ${status}`} role="status">
              {status === "saving" ? (
                <LoaderCircle className="spin" size={14} />
              ) : status === "saved" ? (
                <Check size={14} />
              ) : status === "error" ? (
                <AlertCircle size={14} />
              ) : (
                <span className="dirty-dot" />
              )}
              {status === "saved"
                ? "已保存到本机"
                : status === "saving"
                  ? "正在保存…"
                  : status === "error"
                    ? "尚未保存 · 请检查"
                    : "有修改 · 即将自动保存"}
            </span>
            <Button
              kind="small"
              disabled={busy}
              onClick={() =>
                void safely(async () => {
                  await persist();
                  report("项目已保存到本机");
                })
              }
            >
              <Save size={15} />
              保存
            </Button>
            <Button
              kind="primary small"
              disabled={busy}
              onClick={() => exportContent()}
            >
              <Download size={15} />
              导出内容包
            </Button>
          </div>
        </header>
        <div className="projectbar">
          <div className="project-name-wrap">
            <input
              aria-label="项目名称"
              value={p.name}
              maxLength={80}
              onChange={(e) =>
                change((p) => {
                  p.name = e.target.value;
                })
              }
            />
            <span>把日子写进小世界</span>
          </div>
          <div className="project-tools">
            <Button
              kind="icon"
              onClick={undo}
              disabled={!history.length}
              aria-label="撤销"
              title="撤销"
            >
              <Undo2 size={17} />
            </Button>
            <Button
              kind="icon"
              onClick={redo}
              disabled={!future.length}
              aria-label="重做"
              title="重做"
            >
              <Redo2 size={17} />
            </Button>
            <span className="tool-separator" />
            <Button
              kind="small quiet"
              onClick={() =>
                void safely(() =>
                  withSaved(async () => {
                    setName("我的小世界");
                    setDialog("new");
                  }),
                )
              }
            >
              <Plus size={16} />
              新建
            </Button>
            <Button kind="small quiet" onClick={library}>
              <FolderOpen size={16} />
              打开
            </Button>
            <Button
              kind="small quiet"
              onClick={() =>
                void safely(async () => {
                  const next = await api<Snapshot>(
                    "save-as",
                    latest.current.snapshot,
                  );
                  install(next);
                  report("已另存为独立项目副本");
                })
              }
            >
              另存为
            </Button>
          </div>
        </div>
        {saveError && (
          <div className="save-error" role="alert">
            <AlertCircle size={17} />
            <span>修改仍保留在当前页面。{saveError}</span>
            <Button
              kind="small"
              onClick={() =>
                void safely(async () => {
                  await persist();
                })
              }
            >
              重试保存
            </Button>
          </div>
        )}
        <div className="studio-columns">
          <main className="editor" key={module}>
            {module === "anniversary" && <AnniversaryEditor {...props} />}{" "}
            {module === "letters" && <LettersEditor {...props} />}{" "}
            {module === "memories" && <MemoriesEditor {...props} />}{" "}
            {module === "album" && <AlbumEditor {...props} />}{" "}
            {module === "story" && (
              <StoryEditor {...props} player={player} onPlayer={setPlayer} />
            )}{" "}
            {module === "package" && (
              <PackageEditor
                snapshot={data}
                onExport={() => exportContent()}
                onProjectFile={() => exportContent("lwstudio")}
                onBackup={() => showBackups()}
                onImport={() => input.current?.click()}
              />
            )}
          </main>
          <aside className="preview-panel">
            <div className="preview-top">
              <h2>
                <span className="live-dot" />
                即时设备预览
              </h2>
              <Button
                kind="icon"
                aria-label="放大预览"
                title="放大预览"
                onClick={() => setDialog("preview")}
              >
                <Maximize2 size={17} />
              </Button>
            </div>
            <div className="preview-toolbar">
              <span>Little World · 7-inch</span>
              <div className="theme-switch" aria-label="预览主题">
                {[
                  { id: "sakura", icon: Sun, label: "Sakura 樱花主题" },
                  { id: "moonlight", icon: Moon, label: "Moonlight 深色主题" },
                  { id: "sage", icon: Leaf, label: "Sage 鼠尾草主题" },
                ].map((t) => (
                  <button
                    key={t.id}
                    className={p.theme === t.id ? "active" : ""}
                    aria-label={t.label}
                    title={t.label}
                    onClick={() =>
                      change((p) => {
                        p.theme = t.id as Project["theme"];
                      })
                    }
                  >
                    <t.icon size={15} />
                  </button>
                ))}
              </div>
            </div>
            <Preview
              snapshot={data}
              module={module}
              selected={selected}
              date={previewDate}
              player={player}
              onPlayer={setPlayer}
            />
            <div className="preview-context">
              <div className="section-row">
                <h3>预览设置</h3>
                <span className="tiny-label">STUDIO ONLY</span>
              </div>
              <Field
                label="模拟当地日期"
                hint="可预览天数与信件解锁状态，不修改电脑时间。"
              >
                <input
                  aria-label="模拟当地日期"
                  type="date"
                  min="1970-01-01"
                  max="2200-12-31"
                  value={previewDate}
                  onChange={(e) => {
                    if (e.target.value) setPreviewDate(e.target.value);
                  }}
                />
              </Field>
              <button
                className="reset-date"
                onClick={() => setPreviewDate(localToday())}
              >
                <RotateCcw size={12} />
                回到今天
              </button>
            </div>
            <div className="preview-tip">
              <span>✿</span>
              <div>
                <strong>小小的编辑，实时可见。</strong>
                <p>
                  {module === "story"
                    ? "点击「播放剧情」，在预览中逐句阅读并选择。"
                    : module === "album"
                      ? "预览图片不超过 1024 × 600；原图独立保留。"
                      : module === "letters"
                        ? "调整模拟日期，看看未来的信什么时候打开。"
                        : "左边写下的心意，会在这里慢慢成形。"}
                </p>
              </div>
            </div>
            <div className="preview-footnote">
              <ShieldCheck size={14} />
              <span>离线本机服务 · 无外部请求 · 无 AI 生成</span>
            </div>
            <div className="preview-resource">
              <span>资源占用</span>
              <span>
                {sizeLabel(p.resources.reduce((s, r) => s + r.bytes, 0))} / 40
                MiB
              </span>
            </div>
          </aside>
        </div>
        <footer className="statusbar">
          <span>
            <span className="local-dot" /> LOCAL WORKSPACE{" "}
            <span className="statusbar-path">studio/private/projects</span>
          </span>
          <span>
            {p.demo
              ? "DEMO · 示例内容，请替换为自己的故事"
              : "私人内容 · 仅保存在本机"}
            <span className="statusbar-shortcut">Ctrl + S 保存</span>
          </span>
        </footer>
      </div>
      <input
        hidden
        type="file"
        ref={input}
        accept=".lwpack,.lwstudio,.zip"
        aria-label="导入内容包文件"
        onChange={(e) => {
          const file = e.target.files?.[0];
          if (file)
            void safely(() =>
              withSaved(async () => {
                install(await importPack(file));
                setDialog(null);
                report("已完整导入为新的项目副本");
              }),
            );
          e.target.value = "";
        }}
      />
      {toast && (
        <div className="toast" role="alert">
          <span>{toast}</span>
          <button aria-label="关闭提示" onClick={() => setToast("")}>
            ×
          </button>
        </div>
      )}
      {deleteAction && (
        <Modal title="删除这一页？" onClose={() => setDeleteAction(null)}>
          <p>
            将删除「{deleteAction.name || "未命名内容"}
            」。可使用撤销或项目备份恢复。
          </p>
          <div className="modal-actions">
            <Button onClick={() => setDeleteAction(null)}>保留</Button>
            <Button
              kind="danger"
              onClick={() => {
                deleteAction.action();
                setDeleteAction(null);
              }}
            >
              确认删除
            </Button>
          </div>
        </Modal>
      )}
      {dialog === "new" && (
        <Modal title="开始一本新的小世界" onClose={() => setDialog(null)}>
          <Field label="项目名称">
            <input
              autoFocus
              aria-label="新项目名称"
              maxLength={80}
              value={name}
              onChange={(e) => setName(e.target.value)}
            />
          </Field>
          <p className="muted">
            新项目从留白开始。演示项目中的人物与内容均为 DEMO。
          </p>
          <div className="modal-actions">
            <Button
              disabled={busy}
              onClick={() =>
                void safely(async () => {
                  install(await api<Snapshot>("new", { demo: true }));
                  setDialog(null);
                })
              }
            >
              创建 DEMO
            </Button>
            <Button
              kind="primary"
              disabled={busy || !name.trim()}
              onClick={() =>
                void safely(async () => {
                  install(await api<Snapshot>("new", { name: name.trim() }));
                  setDialog(null);
                })
              }
            >
              <Plus size={16} />
              创建项目
            </Button>
          </div>
        </Modal>
      )}
      {dialog === "library" && (
        <Modal title="打开本地项目" onClose={() => setDialog(null)}>
          <p className="muted">
            保存在当前电脑上的小世界。外部项目可从文件导入。
          </p>
          <div className="library-list">
            {projects.map((info) => (
              <div key={info.id} className="library-item">
                <BookOpen size={23} />
                <div>
                  <strong>{info.name}</strong>
                  <small>
                    {info.updatedAt
                      ? new Date(info.updatedAt).toLocaleString()
                      : "文件校验未通过"}
                  </small>
                </div>
                {info.corrupt ? (
                  <Button kind="small" onClick={() => showBackups(info.id)}>
                    恢复备份
                  </Button>
                ) : (
                  <Button
                    kind="small"
                    onClick={() =>
                      void safely(() =>
                        withSaved(async () => {
                          install(await api<Snapshot>(`project?id=${info.id}`));
                          setDialog(null);
                        }),
                      )
                    }
                  >
                    打开
                  </Button>
                )}
              </div>
            ))}
          </div>
          <div className="modal-actions">
            <Button onClick={() => input.current?.click()}>
              <Upload size={16} />
              导入项目 / 内容包
            </Button>
            <Button
              onClick={() => {
                setName("我的小世界");
                setDialog("new");
              }}
            >
              新建项目
            </Button>
          </div>
        </Modal>
      )}
      {dialog === "backups" && (
        <Modal title="备份与恢复" onClose={() => setDialog(null)}>
          <p className="muted">
            每次保存前保留上一个版本，最多 20 份。恢复会创建独立副本。
          </p>
          <Button onClick={() => exportContent("lwstudio")}>
            <Download size={16} />
            下载当前项目备份
          </Button>
          <div className="library-list">
            {backups.length ? (
              backups.map((n) => (
                <div className="library-item" key={n}>
                  <RotateCcw size={18} />
                  <div>
                    <strong>
                      {new Date(Number(n.slice(0, 13))).toLocaleString()}
                    </strong>
                    <small>保存前的完整项目快照</small>
                  </div>
                  <Button
                    kind="small"
                    onClick={() =>
                      void safely(() =>
                        withSaved(async () => {
                          install(
                            await api<Snapshot>("restore", {
                              id: backupProjectId,
                              name: n,
                            }),
                          );
                          setDialog(null);
                          report("备份已恢复为独立副本");
                        }),
                      )
                    }
                  >
                    恢复副本
                  </Button>
                </div>
              ))
            ) : (
              <Empty
                title="还没有历史备份"
                description="修改并保存一次后，前一个版本会自动保留。"
              />
            )}
          </div>
        </Modal>
      )}
      {dialog === "preview" && (
        <Modal
          wide
          title="Little World · 设备预览"
          onClose={() => setDialog(null)}
        >
          <Preview
            snapshot={data}
            module={module}
            selected={selected}
            date={previewDate}
            player={player}
            onPlayer={setPlayer}
            large
          />
        </Modal>
      )}
    </div>
  );
}
