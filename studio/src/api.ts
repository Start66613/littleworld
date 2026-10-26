import type { Snapshot } from "../shared/model.ts";
let session = "";
export async function connect(): Promise<void> {
  const response = await fetch("/api/session");
  if (!response.ok) throw new Error("无法连接本机 Studio 服务");
  session = (await response.json()).token;
}
export async function api<T>(route: string, body?: unknown): Promise<T> {
  const response = await fetch("/api/" + route, {
    method: body === undefined ? "GET" : "POST",
    headers: {
      "X-Studio-Token": session,
      ...(body === undefined ? {} : { "Content-Type": "application/json" }),
    },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  if (!response.ok) {
    const e = await response
      .json()
      .catch(() => ({ error: "本机服务暂时不可用，请保留页面后重试" }));
    throw new Error(e.error);
  }
  return response.json();
}
export async function importPack(file: File): Promise<Snapshot> {
  if (file.size > 64 * 1024 * 1024) throw new Error("内容包上限为 64 MiB");
  const response = await fetch("/api/import", {
    method: "POST",
    headers: {
      "X-Studio-Token": session,
      "Content-Type": "application/octet-stream",
    },
    body: file,
  });
  if (!response.ok) throw new Error((await response.json()).error);
  return response.json();
}
export function download(
  bytes: BlobPart,
  name: string,
  type = "application/octet-stream",
) {
  const url = URL.createObjectURL(new Blob([bytes], { type })),
    a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export async function exportPack(snapshot: Snapshot, extension = "lwpack") {
  const response = await fetch("/api/export", {
    method: "POST",
    headers: { "X-Studio-Token": session, "Content-Type": "application/json" },
    body: JSON.stringify(snapshot),
  });
  if (!response.ok) throw new Error((await response.json()).error);
  const safeName =
    snapshot.project.name
      .replace(/[<>:"/\\|?*\x00-\x1f]/g, "_")
      .slice(0, 60)
      .replace(/[. ]+$/, "") || "LittleWorld";
  download(
    await response.blob(),
    `LittleWorld-${safeName}.${extension}`,
    "application/zip",
  );
}
