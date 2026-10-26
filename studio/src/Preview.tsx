import { useEffect, useRef, useState } from "react";
import {
  Heart,
  Mail,
  BookOpen,
  Image,
  Leaf,
  ChevronRight,
  BatteryFull,
  Home,
  LockKeyhole,
} from "lucide-react";
import type { Snapshot } from "../shared/model.ts";
import { daysBetween, validDate } from "../shared/date.ts";
import { imageUrl } from "./media.ts";
import { Button, Empty } from "./components.tsx";
export type Module =
  "anniversary" | "letters" | "memories" | "album" | "story" | "package";
export type Player = {
  chapterId: number;
  line: number;
  playing: boolean;
  ended: boolean;
};
export function Preview({
  snapshot,
  module,
  selected,
  date,
  player,
  onPlayer,
  large = false,
}: {
  snapshot: Snapshot;
  module: Module;
  selected: string | number | null;
  date: string;
  player: Player;
  onPlayer: (p: Player) => void;
  large?: boolean;
}) {
  const container = useRef<HTMLDivElement>(null),
    [scale, setScale] = useState(0.5),
    p = snapshot.project;
  useEffect(() => {
    const o = new ResizeObserver((e) =>
      setScale(e[0].contentRect.width / 1024),
    );
    if (container.current) o.observe(container.current);
    return () => o.disconnect();
  }, []);
  const anniversary =
      p.anniversary.find((x) => x.id === selected) ?? p.anniversary[0],
    letter = p.letters.find((x) => x.id === selected) ?? p.letters[0],
    memory = p.memories.find((x) => x.id === selected) ?? p.memories[0],
    photo =
      p.album.photos.find((x) => x.id === selected) ??
      p.album.photos.find((x) => x.id === p.album.coverId) ??
      p.album.photos[0];
  const chapter =
    p.story.chapters.find(
      (c) => c.id === (player.playing ? player.chapterId : Number(selected)),
    ) ?? p.story.chapters[0];
  const line = chapter?.lines[player.playing ? player.line : 0],
    locked = letter?.unlockDate && letter.unlockDate > date;
  const linkedPhoto = p.album.photos.find((x) => x.id === memory?.photoId);
  const advance = () => {
    if (!chapter) return;
    if (player.line < chapter.lines.length - 1)
      onPlayer({ ...player, line: player.line + 1 });
    else if (!chapter.choices.length) onPlayer({ ...player, ended: true });
  };
  return (
    <div className={`device-wrap ${large ? "large" : ""}`}>
      <div className="device-frame">
        <div className="device-viewport" ref={container}>
          <div
            className={`device-screen theme-${p.theme}`}
            style={{ transform: `scale(${scale})` }}
          >
            <div className="device-status">
              <span>
                <Leaf size={18} /> Little World
              </span>
              <span>
                {date.replaceAll("-", " / ")} <BatteryFull size={24} />
              </span>
            </div>
            <div className="device-heading">
              <span className="device-home">
                <Home size={25} />
              </span>
              <h2>
                {
                  {
                    anniversary: "我们的小日子",
                    letters: "今日小信封",
                    memories: "回忆收藏馆",
                    album: "把瞬间珍藏",
                    story: "我们的故事",
                    package: "小世界内容书",
                  }[module]
                }
              </h2>
              <span className="device-demo">
                {p.demo ? "DEMO" : "MY WORLD"}
              </span>
            </div>
            <div className={`device-content ${module}`}>
              {module === "anniversary" &&
                (anniversary ? (
                  <>
                    <div className="day-hero">
                      <div className="orbit one" />
                      <div className="orbit two" />
                      <Heart
                        className="hero-heart"
                        size={43}
                        strokeWidth={1.2}
                      />
                      <p>{anniversary.title || "给这一天起个名字"}</p>
                      <div className="day-number">
                        {validDate(anniversary.date) && validDate(date)
                          ? Math.abs(daysBetween(anniversary.date, date))
                          : "—"}
                        <span>{anniversary.date > date ? "天后" : "天"}</span>
                      </div>
                      <small>
                        {anniversary.date > date
                          ? "在期待的日子里，留一份温柔"
                          : "每一天，都是故事新的一页"}
                      </small>
                      <div className="date-pill">
                        {anniversary.date.replaceAll("-", " . ")}
                      </div>
                    </div>
                    <div className="device-note">
                      <Leaf size={25} />
                      <p>{anniversary.description || "写下一段纪念日说明…"}</p>
                    </div>
                  </>
                ) : (
                  <DeviceEmpty text="添加一个值得记住的日期" />
                ))}
              {module === "letters" &&
                (letter ? (
                  <div className="letter-paper">
                    {locked ? (
                      <div className="locked">
                        <LockKeyhole size={50} />
                        <h3>有一封信，在等那一天</h3>
                        <p>{letter.unlockDate} 开启 · Studio 规则预览</p>
                      </div>
                    ) : (
                      <>
                        <span className="paper-label">
                          <Mail size={20} /> LITTLE LETTER
                        </span>
                        <h3>{letter.title || "给这封信起个标题"}</h3>
                        <p className="paper-subtitle">{letter.subtitle}</p>
                        <div className="paper-body">
                          {letter.body || "在这里，写下想说的话…"}
                        </div>
                        <footer>
                          {letter.date} <Heart size={19} />
                        </footer>
                      </>
                    )}
                  </div>
                ) : (
                  <DeviceEmpty text="写一封想留在这里的信" />
                ))}
              {module === "memories" &&
                (memory?.visible ? (
                  <div className="memory-preview">
                    <div className="memory-picture">
                      {linkedPhoto ? (
                        <img
                          src={imageUrl(snapshot, linkedPhoto.preview)}
                          alt="回忆照片"
                        />
                      ) : (
                        <>
                          <Leaf size={70} strokeWidth={1} />
                          <span>等待一张属于你的照片</span>
                        </>
                      )}
                    </div>
                    <div className="memory-copy">
                      <small>{memory.date}</small>
                      <h3>{memory.title}</h3>
                      <p>{memory.body}</p>
                      <div>
                        {memory.tags.map((t) => (
                          <span key={t} className="device-tag">
                            {t}
                          </span>
                        ))}
                      </div>
                    </div>
                  </div>
                ) : (
                  <DeviceEmpty
                    text={
                      memory
                        ? "这条回忆设为私密，不在设备中显示"
                        : "添加第一条回忆"
                    }
                  />
                ))}
              {module === "album" &&
                (photo ? (
                  <div className="album-preview">
                    <img
                      src={imageUrl(snapshot, photo.preview)}
                      alt={photo.title}
                    />
                    <footer>
                      <span>{photo.title}</span>
                      <small>电脑预览 · 待转换为设备资源</small>
                    </footer>
                  </div>
                ) : (
                  <DeviceEmpty text="选一张照片，把日常留下" />
                ))}
              {module === "story" &&
                (chapter ? (
                  <div className="story-preview">
                    <div className="story-scene">
                      <span className="scene-leaf">✿</span>
                      <small>{chapter.scene || "场景待命名"}</small>
                      <h3>{chapter.title}</h3>
                      <span className="scene-window" />
                      <div className="scene-book">
                        <BookOpen size={68} strokeWidth={1} />
                      </div>
                    </div>
                    <div className="dialogue-box">
                      <strong>
                        {player.ended
                          ? "本段播放结束"
                          : line?.speaker || "说话者"}
                      </strong>
                      <p>
                        {player.ended
                          ? "这一页暂时收好，新的故事等你来写。"
                          : line?.text || "添加对话文字…"}
                      </p>
                      {player.playing &&
                        !player.ended &&
                        (player.line >= chapter.lines.length - 1 &&
                        chapter.choices.length ? (
                          <div className="device-choices">
                            {chapter.choices.map((c, i) => (
                              <button
                                key={i}
                                onClick={() =>
                                  onPlayer(
                                    c.nextId
                                      ? {
                                          chapterId: c.nextId,
                                          line: 0,
                                          playing: true,
                                          ended: false,
                                        }
                                      : { ...player, ended: true },
                                  )
                                }
                              >
                                {c.text || "未命名选项"}{" "}
                                <ChevronRight size={18} />
                              </button>
                            ))}
                          </div>
                        ) : (
                          <button className="dialogue-next" onClick={advance}>
                            继续 <ChevronRight size={18} />
                          </button>
                        ))}
                    </div>
                  </div>
                ) : (
                  <DeviceEmpty text="写下我们的故事第一句" />
                ))}
              {module === "package" && (
                <div className="package-preview">
                  <BookOpen size={70} strokeWidth={1} />
                  <h3>{p.name}</h3>
                  <p>
                    {p.anniversary.length} 个日子 · {p.letters.length} 封信 ·{" "}
                    {p.memories.filter((x) => x.visible).length} 条公开回忆
                  </p>
                  <small>littleworld-content/1 · Studio Preview</small>
                </div>
              )}
            </div>
            <div className="device-dock">
              <Home />
              <Heart />
              <Mail />
              <Image />
              <BookOpen />
            </div>
          </div>
        </div>
      </div>
      <p className="preview-disclaimer">
        Studio Preview，不是 ESP32 实板截图。
        {large ? "" : "显示区域 1024 × 600，自适应缩放。"}
      </p>
    </div>
  );
}
function DeviceEmpty({ text }: { text: string }) {
  return (
    <div className="device-empty">
      <Heart size={55} strokeWidth={1} />
      <p>{text}</p>
    </div>
  );
}
