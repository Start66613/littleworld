import { promises as fs } from "node:fs";
import path from "node:path";
import { emitC, unpack } from "./package.ts";
import { textAdapterIssues } from "../shared/compatibility.ts";
const [input, flag, output] = process.argv.slice(2);
try {
  if (!input || (flag && flag !== "--emit-c") || (flag && !output))
    throw new Error(
      "Usage: npm run validate -- file.lwpack [--emit-c new-file.h]",
    );
  const snap = unpack(await fs.readFile(path.resolve(input)));
  for (const issue of textAdapterIssues(snap.project))
    console.warn("Device text adapter: " + issue);
  if (flag)
    await fs.writeFile(path.resolve(output), emitC(snap.project), {
      encoding: "utf8",
      flag: "wx",
    });
  console.log(
    `Valid ${snap.project.schema}: ${snap.project.memories.length} memories, ${snap.project.album.photos.length} photos, ${snap.project.story.chapters.length} dialogues. Studio only; firmware reader NOT implemented.`,
  );
} catch (e) {
  console.error((e as Error).message);
  process.exitCode = 1;
}
