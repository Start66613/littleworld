import { deflateSync } from "node:zlib";
import { demoProject } from "../shared/model.ts";
import type { Snapshot } from "../shared/model.ts";
import { sha256 } from "../server/package.ts";
function crc(bytes: Buffer) {
  let c = 0xffffffff;
  for (const b of bytes) {
    c ^= b;
    for (let n = 0; n < 8; n++) c = (c >>> 1) ^ (c & 1 ? 0xedb88320 : 0);
  }
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type: string, bytes: Buffer) {
  const head = Buffer.alloc(4),
    tail = Buffer.alloc(4);
  head.writeUInt32BE(bytes.length);
  const body = Buffer.concat([Buffer.from(type), bytes]);
  tail.writeUInt32BE(crc(body));
  return Buffer.concat([head, body, tail]);
}
// Original procedural DEMO fixtures. No private photos, fonts or external assets.
export function demoPng(sage = false, width = 640, height = 420): Buffer {
  const pixels = Buffer.alloc((width * 3 + 1) * height),
    scale = width / 640;
  const glyphs = {
    D: ["1110", "1001", "1001", "1001", "1110"],
    E: ["1111", "1000", "1110", "1000", "1111"],
    M: ["10001", "11011", "10101", "10001", "10001"],
    O: ["0110", "1001", "1001", "1001", "0110"],
  };
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++) {
      const px = x / scale,
        py = y / scale;
      let rgb = sage
        ? [220 - y / 35, 230 - y / 45, 211 - y / 50]
        : [251 - y / 80, 230 - y / 50, 222 - y / 70];
      if ((px - 480) ** 2 + (py - 94) ** 2 < 44 ** 2) rgb = [253, 246, 211];
      if (py > 245 + Math.sin(px / 110) * 35)
        rgb = sage ? [155, 181, 155] : [209, 163, 171];
      if (py > 300 + Math.cos(px / 100) * 28)
        rgb = sage ? [107, 144, 125] : [168, 139, 152];
      if (px > 72 && px < 210 && py > 110 && py < 270) rgb = [252, 245, 230];
      if (px > 89 && px < 194 && py > 127 && py < 253)
        rgb = sage ? [198, 213, 187] : [242, 210, 211];
      let start = 92;
      for (const char of "DEMO") {
        const g = glyphs[char as keyof typeof glyphs],
          gx = Math.floor((px - start) / 4),
          gy = Math.floor((py - 184) / 4);
        if (
          gx >= 0 &&
          gy >= 0 &&
          gy < 5 &&
          gx < g[0].length &&
          g[gy][gx] === "1"
        )
          rgb = [115, 99, 103];
        start += (g[0].length + 2) * 4;
      }
      const offset = y * (width * 3 + 1) + 1 + x * 3;
      pixels[offset] = rgb[0];
      pixels[offset + 1] = rgb[1];
      pixels[offset + 2] = rgb[2];
    }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", ihdr),
    chunk("IDAT", deflateSync(pixels)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}
export function fixture(withPhotos = false): Snapshot {
  const p = demoProject(),
    media: Record<string, string> = {};
  if (withPhotos)
    for (const sage of [false, true]) {
      const id = crypto.randomUUID(),
        bytes = demoPng(sage),
        original = `assets/${id}/original.png`,
        preview = `assets/${id}/preview.png`;
      p.album.photos.push({
        id,
        title: sage ? "一页绿色 · DEMO" : "一页樱色 · DEMO",
        original,
        preview,
        width: 640,
        height: 420,
        previewWidth: 640,
        previewHeight: 420,
        deviceStatus: "pending",
        estimatedRGB565Bytes: 640 * 420 * 2,
      });
      for (const [path, role] of [
        [original, "original"],
        [preview, "preview"],
      ] as const) {
        p.resources.push({
          path,
          role,
          mime: "image/png",
          bytes: bytes.length,
          sha256: sha256(bytes),
        });
        media[path] = bytes.toString("base64");
      }
    }
  p.album.coverId = p.album.photos[0]?.id ?? null;
  if (withPhotos) p.memories[0].photoId = p.album.photos[0].id;
  return { project: p, media };
}
