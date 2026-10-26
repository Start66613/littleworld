import type { Photo, Project, Snapshot } from "../shared/model.ts";
import { LIMITS } from "../shared/model.ts";
export function imageUrl(snapshot: Snapshot, path: string): string {
  const r = snapshot.project.resources.find((r) => r.path === path);
  return r && snapshot.media[path]
    ? `data:${r.mime};base64,${snapshot.media[path]}`
    : "";
}
const base64 = (blob: Blob): Promise<string> =>
  new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result).split(",")[1]);
    reader.onerror = () => reject(new Error("无法读取照片"));
    reader.readAsDataURL(blob);
  });
async function resource(
  blob: Blob,
  path: string,
  role: "original" | "preview",
  mime: "image/png" | "image/jpeg" | "image/webp",
) {
  const bytes = await blob.arrayBuffer(),
    digest = await crypto.subtle.digest("SHA-256", bytes);
  return {
    descriptor: {
      path,
      role,
      mime,
      bytes: blob.size,
      sha256: [...new Uint8Array(digest)]
        .map((x) => x.toString(16).padStart(2, "0"))
        .join(""),
    },
    base64: await base64(blob),
  };
}
export async function preparePhoto(
  file: File,
): Promise<{
  photo: Photo;
  resources: Project["resources"];
  media: Record<string, string>;
}> {
  if (file.size > LIMITS.file || file.size === 0)
    throw new Error("每张照片须大于 0 且不超过 10 MiB");
  const head = new Uint8Array(await file.slice(0, 32).arrayBuffer());
  const mime =
    head[0] === 137 && head[1] === 80
      ? "image/png"
      : head[0] === 255 && head[1] === 216
        ? "image/jpeg"
        : String.fromCharCode(...head.slice(8, 12)) === "WEBP"
          ? "image/webp"
          : null;
  if (!mime) throw new Error("请选择 PNG、JPEG 或 WebP 图片（不接受 SVG）");
  // PNG dimensions can be checked before decode.
  if (mime === "image/png" && head.length >= 24) {
    const v = new DataView(head.buffer);
    if (v.getUint32(16) * v.getUint32(20) > 40_000_000)
      throw new Error("图片超过 4000 万像素，请先缩小");
  }
  const bitmap = await createImageBitmap(file).catch(() => {
    throw new Error("图片无法解码，可能已损坏");
  });
  try {
    if (
      bitmap.width > 20000 ||
      bitmap.height > 20000 ||
      bitmap.width * bitmap.height > 40_000_000
    )
      throw new Error("图片超过尺寸上限（边长 20000，4000 万像素）");
    const scale = Math.min(1, 1024 / bitmap.width, 600 / bitmap.height),
      width = Math.max(1, Math.round(bitmap.width * scale)),
      height = Math.max(1, Math.round(bitmap.height * scale));
    const canvas = document.createElement("canvas");
    canvas.width = width;
    canvas.height = height;
    const context = canvas.getContext("2d");
    if (!context) throw new Error("浏览器不支持图片预览处理");
    context.drawImage(bitmap, 0, 0, width, height);
    const png = await new Promise<Blob>((resolve, reject) =>
      canvas.toBlob(
        (b) => (b ? resolve(b) : reject(new Error("图片预处理失败"))),
        "image/png",
      ),
    );
    const id = crypto.randomUUID(),
      ext = { "image/png": "png", "image/jpeg": "jpg", "image/webp": "webp" }[
        mime
      ];
    const original = await resource(
        file,
        `assets/${id}/original.${ext}`,
        "original",
        mime,
      ),
      preview = await resource(
        png,
        `assets/${id}/preview.png`,
        "preview",
        "image/png",
      );
    return {
      photo: {
        id,
        title: file.name.replace(/\.[^.]+$/, "").slice(0, 80),
        original: original.descriptor.path,
        preview: preview.descriptor.path,
        width: bitmap.width,
        height: bitmap.height,
        previewWidth: width,
        previewHeight: height,
        deviceStatus: "pending",
        estimatedRGB565Bytes: width * height * 2,
      },
      resources: [original.descriptor, preview.descriptor],
      media: {
        [original.descriptor.path]: original.base64,
        [preview.descriptor.path]: preview.base64,
      },
    };
  } finally {
    bitmap.close();
  }
}
export const sizeLabel = (bytes: number) =>
  bytes < 1024
    ? `${bytes} B`
    : bytes < 1048576
      ? `${(bytes / 1024).toFixed(1)} KiB`
      : `${(bytes / 1048576).toFixed(1)} MiB`;
