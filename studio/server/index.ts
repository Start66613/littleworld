import http from "node:http";
import { promises as fs } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { randomUUID, timingSafeEqual } from "node:crypto";
import { demoProject, emptyProject, LIMITS } from "../shared/model.ts";
import { pack, unpack, verifySnapshot } from "./package.ts";
import { Store } from "./storage.ts";
const studio = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
export function createStudioServer(
  root = path.join(studio, "private", "projects"),
) {
  const store = new Store(root),
    token = randomUUID();
  const allowed = new Set([
    "127.0.0.1:4317",
    "localhost:4317",
    "127.0.0.1:4318",
    "localhost:4318",
  ]);
  const json = (res: http.ServerResponse, status: number, value: unknown) => {
    res.writeHead(status, {
      "Content-Type": "application/json; charset=utf-8",
    });
    res.end(JSON.stringify(value));
  };
  return http.createServer(async (req, res) => {
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.setHeader("Cache-Control", "no-store");
    res.setHeader("Referrer-Policy", "no-referrer");
    res.setHeader(
      "Content-Security-Policy",
      "default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; font-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'",
    );
    try {
      if (
        !allowed.has(req.headers.host ?? "") &&
        ![
          `127.0.0.1:${req.socket.localPort}`,
          `localhost:${req.socket.localPort}`,
        ].includes(req.headers.host ?? "")
      )
        return json(res, 403, { error: "只允许本机访问" });
      const origin = req.headers.origin;
      if (
        origin &&
        ![
          "http://127.0.0.1:4317",
          "http://localhost:4317",
          "http://127.0.0.1:4318",
          "http://localhost:4318",
        ].includes(origin)
      )
        return json(res, 403, { error: "拒绝跨站请求" });
      if (req.headers["sec-fetch-site"] === "cross-site")
        return json(res, 403, { error: "拒绝跨站请求" });
      const url = new URL(req.url ?? "/", "http://127.0.0.1:4317"),
        route = url.pathname;
      if (route === "/api/session" && req.method === "GET")
        return json(res, 200, { token, local: true, version: "1.0.0" });
      if (route.startsWith("/api/")) {
        const header = String(req.headers["x-studio-token"] ?? "");
        if (
          header.length !== token.length ||
          !timingSafeEqual(Buffer.from(header), Buffer.from(token))
        )
          return json(res, 403, { error: "会话已失效，请刷新 Studio" });
        if (route === "/api/projects" && req.method === "GET")
          return json(res, 200, await store.list());
        if (route === "/api/project" && req.method === "GET")
          return json(
            res,
            200,
            await store.open(url.searchParams.get("id") ?? ""),
          );
        if (route === "/api/backups" && req.method === "GET")
          return json(
            res,
            200,
            await store.backups(url.searchParams.get("id") ?? ""),
          );
        if (req.method !== "POST")
          return json(res, 405, { error: "不支持的操作" });
        const max = route === "/api/import" ? LIMITS.archive : 60 * 1024 * 1024;
        if (Number(req.headers["content-length"]) > max)
          return json(res, 413, { error: "文件过大" });
        const chunks: Buffer[] = [];
        let size = 0;
        for await (const chunk of req) {
          size += chunk.length;
          if (size > max) {
            json(res, 413, { error: "文件过大" });
            req.destroy();
            return;
          }
          chunks.push(Buffer.from(chunk));
        }
        const body = Buffer.concat(chunks);
        if (route === "/api/import") {
          const snap = unpack(body);
          snap.project.id = randomUUID();
          snap.project.updatedAt = new Date().toISOString();
          await store.save(snap, true);
          return json(res, 200, snap);
        }
        let data;
        try {
          data = JSON.parse(
            new TextDecoder("utf-8", { fatal: true }).decode(body),
          );
        } catch {
          return json(res, 400, { error: "JSON 已损坏或不是 UTF-8" });
        }
        if (route === "/api/new") {
          const snap =
            data.demo === true
              ? await fs
                  .readFile(path.join(studio, "examples", "demo.lwpack"))
                  .then(unpack)
                  .catch(() => ({ project: demoProject(), media: {} }))
              : {
                  project: emptyProject(
                    typeof data.name === "string" ? data.name : undefined,
                  ),
                  media: {},
                };
          snap.project.id = randomUUID();
          snap.project.createdAt = snap.project.updatedAt =
            new Date().toISOString();
          await store.save(snap, true);
          return json(res, 200, snap);
        }
        if (route === "/api/save") {
          if (typeof data.expectedUpdatedAt !== "string")
            throw new Error("缺少原文件版本，拒绝覆盖");
          const snap = verifySnapshot(data.snapshot);
          await store.save(snap, false, data.expectedUpdatedAt);
          return json(res, 200, { savedAt: snap.project.updatedAt });
        }
        if (route === "/api/save-as") {
          const snap = verifySnapshot(data);
          snap.project.id = randomUUID();
          snap.project.name = snap.project.name.slice(0, 75) + " · 副本";
          snap.project.createdAt = snap.project.updatedAt =
            new Date().toISOString();
          await store.save(snap, true);
          return json(res, 200, snap);
        }
        if (route === "/api/restore") {
          const snap = await store.readBackup(
            String(data.id),
            String(data.name),
          );
          snap.project.id = randomUUID();
          snap.project.name += " · 恢复";
          snap.project.updatedAt = new Date().toISOString();
          await store.save(snap, true);
          return json(res, 200, snap);
        }
        if (route === "/api/export") {
          const bytes = pack(verifySnapshot(data));
          res.writeHead(200, {
            "Content-Type": "application/zip",
            "Content-Disposition":
              'attachment; filename="littleworld-content.lwpack"',
          });
          return res.end(bytes);
        }
        return json(res, 404, { error: "操作不存在" });
      }
      if (req.method !== "GET")
        return json(res, 405, { error: "不支持的操作" });
      if (
        route !== "/" &&
        !/^\/(assets\/[a-zA-Z0-9_.-]+|favicon\.svg)$/.test(route)
      )
        return json(res, 404, { error: "文件不存在" });
      const file = path.join(
        studio,
        "dist",
        route === "/" ? "index.html" : route.slice(1),
      );
      const bytes = await fs.readFile(file);
      const mime =
        (
          {
            ".html": "text/html; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".svg": "image/svg+xml",
          } as Record<string, string>
        )[path.extname(file)] ?? "application/octet-stream";
      res.writeHead(200, { "Content-Type": mime });
      res.end(bytes);
    } catch (error) {
      const e = error as NodeJS.ErrnoException;
      const message =
        e.code === "ENOENT"
          ? "找不到项目或构建文件，请检查文件或使用启动脚本。"
          : e.code
            ? "本地文件操作失败。检查磁盘空间与文件权限；原文件和备份会保留。"
            : e.message || "操作失败";
      if (!res.headersSent) json(res, 400, { error: message });
      else res.end();
    }
  });
}
if (
  process.argv[1] &&
  path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const server = createStudioServer(process.env.LW_STUDIO_DATA);
  server.on("error", () => {
    console.error(
      "Studio could not start: port 4317 is occupied. Close the previous Studio window.",
    );
    process.exitCode = 1;
  });
  server.listen(4317, "127.0.0.1", () =>
    console.log("Little World Studio http://127.0.0.1:4317 (local only)"),
  );
}
