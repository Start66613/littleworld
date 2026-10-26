import type { Project } from "./model.ts";
export function textAdapterIssues(p: Project): string[] {
  const visible = p.memories.filter((m) => m.visible),
    issues: string[] = [],
    encoder = new TextEncoder();
  if (visible.length > 32)
    issues.push(
      `公开回忆 ${visible.length} 条，lw_memories_set_source() 上限为 32 条。`,
    );
  for (const m of visible) {
    if (encoder.encode(m.title).length > 128)
      issues.push(`「${m.title.slice(0, 20)}」标题超出 128 UTF-8 字节。`);
    if (encoder.encode(m.body).length > 4096)
      issues.push(`「${m.title.slice(0, 20)}」正文超出 4096 UTF-8 字节。`);
  }
  return issues;
}
