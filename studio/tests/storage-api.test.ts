import { test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { promises as fs } from "node:fs";
import path from "node:path";
import { Store, projectDir } from "../server/storage.ts";
import { createStudioServer } from "../server/index.ts";
import { fixture } from "./fixtures.ts";
import { pack, unpack } from "../server/package.ts";
async function temporary<T>(run: (dir: string) => Promise<T>) {
  const parent = path.resolve("private", "test-runs");
  await fs.mkdir(parent, { recursive: true });
  const dir = await fs.mkdtemp(path.join(parent, "run-"));
  try {
    return await run(dir);
  } finally {
    assert.ok(path.resolve(dir).startsWith(parent + path.sep));
    await fs.rm(dir, { recursive: true, force: true });
  }
}
test("atomic save, reopen, backup, restore and exclusive create", () =>
  temporary(async (dir) => {
    const store = new Store(dir),
      s = fixture(true);
    await store.save(s, true);
    assert.deepEqual((await store.open(s.project.id)).project, s.project);
    await assert.rejects(() => store.save(s, true), /存在/);
    const old = s.project.updatedAt;
    s.project.name = "保存后的中文名字";
    s.project.updatedAt = new Date(Date.now() + 1).toISOString();
    await store.save(s, false, old);
    assert.equal((await store.list())[0].name, s.project.name);
    const names = await store.backups(s.project.id);
    assert.equal(names.length, 1);
    assert.notEqual(
      (await store.readBackup(s.project.id, names[0])).project.name,
      s.project.name,
    );
    assert.ok(
      !(await fs.readdir(projectDir(dir, s.project.id))).some((n) =>
        n.endsWith(".tmp"),
      ),
    );
  }));
test("stale writer cannot overwrite a newer version", () =>
  temporary(async (dir) => {
    const store = new Store(dir),
      s = fixture();
    await store.save(s, true);
    const old = s.project.updatedAt;
    s.project.updatedAt = new Date(Date.now() + 10).toISOString();
    await store.save(s, false, old);
    s.project.name = "stale";
    await assert.rejects(() => store.save(s, false, old), /另一个窗口/);
    assert.notEqual((await store.open(s.project.id)).project.name, "stale");
  }));
test("corrupt files remain listed and last valid backup is readable", () =>
  temporary(async (dir) => {
    const store = new Store(dir),
      s = fixture();
    await store.save(s, true);
    await store.save(s);
    await fs.writeFile(
      path.join(projectDir(dir, s.project.id), "project.lwstudio"),
      "corrupt",
    );
    assert.equal((await store.list())[0].corrupt, true);
    assert.equal(
      (
        await store.readBackup(
          s.project.id,
          (await store.backups(s.project.id))[0],
        )
      ).project.schema,
      "littleworld-content/1",
    );
  }));
test("project path and backup filename traversal rejected", () =>
  temporary(async (dir) => {
    const store = new Store(dir);
    assert.throws(() => projectDir(dir, "../../secret"));
    await assert.rejects(() =>
      store.readBackup(crypto.randomUUID(), "../../file"),
    );
  }));
test("HTTP create, save, export, import, token/origin checks and malformed input", () =>
  temporary(async (dir) => {
    const server = createStudioServer(dir);
    await new Promise<void>((r) => server.listen(0, "127.0.0.1", r));
    const address = server.address();
    assert.ok(address && typeof address === "object");
    const base = `http://127.0.0.1:${address.port}`;
    const headers: Record<string, string> = { Host: "127.0.0.1:4317" };
    const call = (route: string, body?: unknown) =>
      fetch(base + "/api/" + route, {
        headers: {
          ...headers,
          ...(body === undefined ? {} : { "Content-Type": "application/json" }),
        },
        method: body === undefined ? "GET" : "POST",
        body: body === undefined ? undefined : JSON.stringify(body),
      });
    try {
      assert.equal((await call("projects")).status, 403);
      headers["X-Studio-Token"] = (await (await call("session")).json()).token;
      const created = await (
        await call("new", { name: "测试 / 中文 : CON ?" })
      ).json();
      assert.equal(created.project.name, "测试 / 中文 : CON ?");
      const stamp = created.project.updatedAt;
      created.project.name = "编辑后的名字";
      created.project.updatedAt = new Date(Date.now() + 100).toISOString();
      assert.equal(
        (await call("save", { snapshot: created, expectedUpdatedAt: stamp }))
          .status,
        200,
      );
      assert.equal(
        (await call("save", { snapshot: created, expectedUpdatedAt: stamp }))
          .status,
        400,
      );
      const exported = await call("export", created);
      assert.equal(exported.status, 200);
      const bytes = await exported.arrayBuffer();
      assert.equal(unpack(new Uint8Array(bytes)).project.name, "编辑后的名字");
      const imported = await fetch(base + "/api/import", {
        method: "POST",
        headers,
        body: bytes,
      });
      assert.equal(imported.status, 200);
      assert.notEqual((await imported.json()).project.id, created.project.id);
      assert.equal(
        (
          await fetch(base + "/api/new", {
            method: "POST",
            headers: { ...headers, Origin: "https://evil.example" },
            body: "{}",
          })
        ).status,
        403,
      );
      assert.equal(
        (
          await fetch(base + "/api/new", {
            method: "POST",
            headers,
            body: "{broken",
          })
        ).status,
        400,
      );
      assert.equal(
        (
          await fetch(base + "/api/import", {
            method: "POST",
            headers,
            body: "bad zip",
          })
        ).status,
        400,
      );
      const status = await new Promise<number | undefined>(
        (resolve, reject) => {
          http
            .get(
              base + "/api/session",
              { headers: { Host: "evil.example:4317" } },
              (res) => {
                res.resume();
                resolve(res.statusCode);
              },
            )
            .on("error", reject);
        },
      );
      assert.equal(status, 403);
    } finally {
      await new Promise<void>((resolve, reject) =>
        server.close((e) => (e ? reject(e) : resolve())),
      );
    }
  }));
