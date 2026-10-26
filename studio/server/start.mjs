import { spawn, execFile } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const dev = process.argv.includes("--dev");
const children = [];
const server = spawn(process.execPath, ["server/index.ts"], {
  cwd: root,
  stdio: "inherit",
});
children.push(server);
if (dev)
  children.push(
    spawn(process.execPath, ["node_modules/vite/bin/vite.js"], {
      cwd: root,
      stdio: "inherit",
    }),
  );
for (const child of children)
  child.on("error", (e) => {
    console.error(e.message);
    for (const c of children) c.kill();
    process.exitCode = 1;
  });
server.on("exit", (code) => {
  for (const child of children) if (child !== server) child.kill();
  process.exitCode = code ?? 1;
});
if (process.argv.includes("--open")) {
  const url = `http://127.0.0.1:${dev ? 4318 : 4317}`;
  setTimeout(() => {
    if (process.platform === "win32")
      execFile("rundll32.exe", ["url.dll,FileProtocolHandler", url]);
  }, 1200);
}
process.on("SIGINT", () => {
  for (const c of children) c.kill();
  process.exit();
});
