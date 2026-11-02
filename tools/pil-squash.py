#!/usr/bin/env python3
"""Rebuild a split Qualcomm PIL image (.mdt + .bNN) into a single .mbn ELF.

Same approach as linux-msm/pil-squasher: start from the .mdt (ELF header,
program headers, hash segment) and place each .bNN segment at its p_offset.

usage: pil-squash.py <out.mbn> <in.mdt>
"""
import os
import struct
import sys


def phdrs(data):
    if data[:4] != b"\x7fELF":
        raise SystemExit("not an ELF")
    is64 = data[4] == 2
    if is64:
        phoff, = struct.unpack_from("<Q", data, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
        fmt = "<IIQQQQQQ"  # type, flags, offset, vaddr, paddr, filesz, memsz, align
        for i in range(phnum):
            t, _, off, _, _, filesz, _, _ = struct.unpack_from(fmt, data, phoff + i * phentsize)
            yield i, t, off, filesz
    else:
        phoff, = struct.unpack_from("<I", data, 0x1C)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
        fmt = "<IIIIIIII"  # type, offset, vaddr, paddr, filesz, memsz, flags, align
        for i in range(phnum):
            t, off, _, _, filesz, _, _, _ = struct.unpack_from(fmt, data, phoff + i * phentsize)
            yield i, t, off, filesz


def main():
    out, mdt = sys.argv[1], sys.argv[2]
    base = mdt[:-4]
    hdr = open(mdt, "rb").read()
    img = bytearray(hdr)
    for i, _, off, filesz in phdrs(hdr):
        if not filesz:
            continue
        seg = "%s.b%02d" % (base, i)
        if os.path.exists(seg):
            data = open(seg, "rb").read()
        elif off + filesz <= len(hdr):
            continue  # already inside the .mdt (headers / hash segment)
        else:
            raise SystemExit("missing segment %s" % seg)
        if len(data) != filesz:
            raise SystemExit("%s: size %d != p_filesz %d" % (seg, len(data), filesz))
        if len(img) < off + filesz:
            img.extend(b"\0" * (off + filesz - len(img)))
        img[off:off + filesz] = data
    open(out, "wb").write(img)
    print("%s: %d bytes" % (out, len(img)))


if __name__ == "__main__":
    main()
