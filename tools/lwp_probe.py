"""Read LWPX pack headers: geometry, frame bytes, clip table, frame-count budget.

Read-only probe used for the V4.4 sprite-cache report; it never writes to the
pack files. Usage:  python lwp_probe.py <pack> [<pack> ...]
"""
import struct
import sys
import os

CLIP_RECORD = 108


def probe(path):
    with open(path, "rb") as fh:
        data = fh.read()
    out = {}
    out["file"] = os.path.basename(path)
    out["bytes"] = len(data)
    out["magic"] = data[:8].decode("latin1", "replace")
    w, h = struct.unpack_from("<HH", data, 8)
    out["width"] = w
    out["height"] = h
    out["frame_bytes"] = w * h * 3
    # header layout after geometry: u16 clip_count, u16 frame_count (observed)
    out["hdr12_44"] = data[12:44].hex()
    clips = struct.unpack_from("<H", data, 12)[0]
    frames = struct.unpack_from("<H", data, 14)[0]
    out["clips"] = clips
    out["frames"] = frames
    table = 16
    names = []
    for i in range(min(clips, 64)):
        rec = data[table + i * CLIP_RECORD: table + (i + 1) * CLIP_RECORD]
        if len(rec) < CLIP_RECORD:
            break
        name = rec[:32].split(b"\x00", 1)[0].decode("latin1", "replace")
        count = struct.unpack_from("<H", rec, 32)[0]
        anchor_x, anchor_y = struct.unpack_from("<hh", rec, 34)
        loop = rec[38]
        offset = struct.unpack_from("<I", rec, 40)[0]
        durations = list(struct.unpack_from("<%dH" % min(count, 30), rec, 44))
        names.append((name, count, anchor_x, anchor_y, loop, offset, durations[:8]))
    out["clip_table"] = names
    return out


def main(argv):
    for p in argv[1:]:
        info = probe(p)
        print("== %s  %d B  %s  %dx%d  frame=%d B  clips=%d frames=%d"
              % (info["file"], info["bytes"], info["magic"], info["width"],
                 info["height"], info["frame_bytes"], info["clips"], info["frames"]))
        print("   hdr[12:44]=%s" % info["hdr12_44"])
        for name, count, ax, ay, loop, off, durs in info["clip_table"]:
            print("   clip %-24s frames=%-3d anchor=%d,%d loop=%d off=%d dur=%s"
                  % (name, count, ax, ay, loop, off, durs))
        print()


if __name__ == "__main__":
    main(sys.argv)
