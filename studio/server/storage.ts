import { promises as fs } from "node:fs";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { pack, unpack } from "./package.ts";
import type { Snapshot } from "../shared/model.ts";

const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
export function projectDir(root: string, id: string): string {
  if (!uuid.test(id)) throw new Error("无效的项目 ID");
  return path.join(root, id);
}
export async function atomicWrite(
  file: string,
  data: Uint8Array,
): Promise<void> {
  await fs.mkdir(path.dirname(file), { recursive: true });
  const temp = `${file}.${randomUUID()}.tmp`;
  let handle;
  try {
    handle = await fs.open(temp, "wx");
    await handle.writeFile(data);
    await handle.sync();
    await handle.close();
    handle = undefined;
    await fs.rename(temp, file);
  } finally {
    if (handle) await handle.close();
    await fs.rm(temp, { force: true }).catch(() => {});
  }
}
export class Store {
  root: string;
  queues = new Map<string, Promise<unknown>>();
  constructor(root: string) {
    this.root = path.resolve(root);
  }
  async list() {
    await fs.mkdir(this.root, { recursive: true });
    const out = [];
    for (const id of await fs.readdir(this.root)) {
      if (!uuid.test(id)) continue;
      try {
        const { project: p } = await this.open(id);
        out.push({ id, name: p.name, updatedAt: p.updatedAt, demo: p.demo });
      } catch {
        out.push({
          id,
          name: "损坏的项目（可从备份恢复）",
          updatedAt: "",
          demo: false,
          corrupt: true,
        });
      }
    }
    return out.sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
  }
  async open(id: string): Promise<Snapshot> {
    return unpack(
      await fs.readFile(
        path.join(projectDir(this.root, id), "project.lwstudio"),
      ),
    );
  }
  async backups(id: string) {
    const dir = path.join(projectDir(this.root, id), "backups");
    return (await fs.readdir(dir).catch(() => []))
      .filter((n) => /^\d{13}-[a-f0-9-]{36}\.lwstudio$/.test(n))
      .sort()
      .reverse();
  }
  async readBackup(id: string, name: string) {
    if (!(await this.backups(id)).includes(name)) throw new Error("备份不存在");
    return unpack(
      await fs.readFile(path.join(projectDir(this.root, id), "backups", name)),
    );
  }
  async save(snapshot: Snapshot, create = false, expectedUpdatedAt?: string) {
    const id = snapshot.project.id,
      dir = projectDir(this.root, id);
    const before = this.queues.get(id) ?? Promise.resolve();
    const task = before
      .catch(() => {})
      .then(async () => {
        const file = path.join(dir, "project.lwstudio");
        const bytes = pack(snapshot);
        const old = await fs
          .readFile(file)
          .catch((e: NodeJS.ErrnoException) => {
            if (e.code !== "ENOENT") throw e;
            return null;
          });
        if (create && old) throw new Error("项目已存在，请另存为");
        if (
          expectedUpdatedAt !== undefined &&
          (!old || unpack(old).project.updatedAt !== expectedUpdatedAt)
        )
          throw new Error(
            "项目已在另一个窗口更新。请使用「另存为」保留当前修改，再重新打开原项目。",
          );
        if (old) {
          await atomicWrite(
            path.join(dir, "backups", `${Date.now()}-${randomUUID()}.lwstudio`),
            old,
          );
        }
        await atomicWrite(file, bytes);
        const names = await this.backups(id);
        for (const name of names.slice(20))
          await fs.unlink(path.join(dir, "backups", name));
      });
    this.queues.set(id, task);
    try {
      await task;
    } finally {
      if (this.queues.get(id) === task) this.queues.delete(id);
    }
  }
}
