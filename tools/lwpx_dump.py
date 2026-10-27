import struct, os, sys

files = [
    "assets/visual-v4-3/runtime/skin.lwp",
    "assets/visual-v4-3/runtime/daily.lwp",
    "assets/visual-v4-2/runtime/polish.lwp",
    "assets/visual-v4-2/runtime/ui.lwp",
    "assets/visual-v4/runtime/male.lwp",
    "assets/visual-v4/runtime/qianqian-wave.lwp",
]

for f in files:
    d = open(f, "rb").read()
    w, h, clips = struct.unpack("<HHH", d[8:14])
    print("--- %s  %dx%d clips=%d size=%d" % (f, w, h, clips, len(d)))
    for i in range(clips):
        r = d[16 + i * 108: 16 + (i + 1) * 108]
        name = r[:32].split(b"\x00")[0].decode("ascii", "replace")
        frames, ax, ay = struct.unpack("<Hhh", r[32:38])
        loop = r[38]
        off = struct.unpack("<I", r[40:44])[0]
        durs = struct.unpack("<32H", r[44:108])[:frames]
        need = off + frames * w * h * 3
        print("   %-24s frames=%3d anchor=(%d,%d) loop=%d off=%d dur=%s need=%d file=%d %s"
              % (name, frames, ax, ay, loop, off, durs[:4], need, len(d),
                 "OK" if need <= len(d) else "SHORT"))
