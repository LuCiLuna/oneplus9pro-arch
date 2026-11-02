#!/usr/bin/env python3
"""Dump the register lists from a CamX sensor module blob.

usage: chromatix-regs.py <com.qti.sensormodule.*.bin> [list-name-filter]
       chromatix-regs.py <bin> --nodes   (every small node: u32 / double / string)

Layout ("Parameter Parser V3"): the header lists sections as (type, offset,
size) triples at 0xa8; section 1 holds the node records, section 2 the data.
A node record is <id u32><name char[40]><0xffffffff><data offset><size>.
A regSetting array is a run of 40-byte entries in the data section:
  slaveAddr-present, slaveAddr-id, registerAddr, data count,
  registerData-id, addrType, dataType, operation, delay-present, delayUs-id
and each registerData node's record points at its values (u32 each).
"""
import struct
import sys


def main():
    b = open(sys.argv[1], "rb").read()
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    sec = struct.unpack_from("<9I", b, 0xa8)
    nodes_off, nodes_size = sec[1], sec[2]
    data_off = sec[4]

    nodes = {}
    order = []
    p = nodes_off
    end = nodes_off + nodes_size
    while p + 60 <= end:
        if struct.unpack_from("<I", b, p + 44)[0] != 0xffffffff:
            p += 4
            continue
        nid = struct.unpack_from("<I", b, p)[0]
        name = b[p + 4:p + 44].split(b"\0")[0]
        if not name or not name.isascii() or not name[:1].isalpha():
            p += 4
            continue
        off, size = struct.unpack_from("<II", b, p + 48)
        nodes[nid] = (name.decode(), off, size)
        order.append(nid)
        p += 56

    if flt == "--nodes":
        for nid in order:
            name, off, size = nodes[nid]
            if not size or size > 64:
                continue
            raw = b[data_off + off:data_off + off + size]
            txt = raw.split(b"\0")[0]
            words = struct.unpack_from("<%dI" % (size // 4), raw)
            dbl = struct.unpack_from("<d", raw)[0] if size == 8 else None
            print("%-36s %s%s%s" % (name, " ".join("0x%x" % w for w in words),
                  "  (%g)" % dbl if dbl is not None else "",
                  "  %r" % txt.decode() if len(txt) > 2 and txt.isascii() and txt.decode().isprintable() else ""))
        return

    ctx = []
    for nid in order:
        name, off, size = nodes[nid]
        if name != "regSetting":
            if name not in ("slaveAddr", "registerData", "delayUs"):
                ctx = (ctx + [name])[-3:]
            continue
        if size == 0 or size % 40:
            continue
        label = "/".join(ctx)
        if flt and flt not in label:
            continue
        print("== %s (%d regs)" % (label, size // 40))
        for e in range(data_off + off, data_off + off + size, 40):
            w = struct.unpack_from("<10I", b, e)
            addr, cnt, did, atype, dtype, op = w[2], w[3], w[4], w[5], w[6], w[7]
            dname, doff, dsize = nodes.get(did, ("?", 0, 0))
            vals = struct.unpack_from("<%dI" % (dsize // 4), b, data_off + doff) if dsize else ()
            delay = 0
            if w[8] and w[9] in nodes and nodes[w[9]][2] == 4:
                delay = struct.unpack_from("<I", b, data_off + nodes[w[9]][1])[0]
            print("  0x%04x = %s  (a%d d%d op%d%s)" % (
                addr, " ".join("0x%02x" % v for v in vals), atype, dtype, op,
                ", delay %d us" % delay if delay else ""))


if __name__ == "__main__":
    main()
