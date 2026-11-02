#!/usr/bin/env python3
"""Decode a downstream (qcom,mdss-dsi-*) panel timing node into a readable
listing of DSI commands and mode/DSC parameters.

usage: dsi-panel-extract.py <panel-node.dts> <timing-name> [prop ...]
  default props: qcom,mdss-dsi-on-command qcom,mdss-dsi-off-command

Command arrays are packed as: dtype last vc ack wait_ms len_hi len_lo payload...
"""
import re
import sys

DTYPES = {
    0x05: "dcs_short_noparam", 0x15: "dcs_short_param", 0x39: "dcs_long",
    0x03: "gen_short_noparam", 0x13: "gen_short_1param", 0x23: "gen_short_2param",
    0x29: "gen_long", 0x0a: "pps", 0x07: "compression_mode", 0x06: "dcs_read",
}


def node_text(dts, name):
    i = dts.index(name + " {")
    depth, j = 0, i
    while True:
        c = dts[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return dts[i:j + 1]
        j += 1


def prop(text, name):
    m = re.search(r"\n\s*" + re.escape(name) + r" = (.*?);", text, re.S)
    return m.group(1).strip() if m else None


def u32s(v):
    return [int(x, 16) for x in re.findall(r"0x[0-9a-f]+", v)] if v else []


def decode(arr):
    if arr.lstrip().startswith("<"):
        # dtc prints byte arrays whose length is a multiple of 4 as u32 cells
        b = []
        for w in re.findall(r"0x[0-9a-f]+", arr):
            b += list(int(w, 16).to_bytes(4, "big"))
    else:
        b = [int(x, 16) for x in arr.strip("[]").split()]
    out, i = [], 0
    while i + 7 <= len(b):
        dtype, last, vc, ack, wait, n = b[i], b[i + 1], b[i + 2], b[i + 3], b[i + 4], (b[i + 5] << 8) | b[i + 6]
        payload = b[i + 7:i + 7 + n]
        out.append((dtype, last, wait, payload))
        i += 7 + n
    return out


def main():
    dts = open(sys.argv[1]).read()
    timing = node_text(dts, sys.argv[2])
    props = sys.argv[3:] or ["qcom,mdss-dsi-on-command", "qcom,mdss-dsi-off-command"]
    keys = ["panel-width", "panel-height", "h-front-porch", "h-back-porch", "h-pulse-width",
            "v-front-porch", "v-back-porch", "v-pulse-width", "panel-framerate", "panel-clockrate"]
    print("# mode", {k: u32s(prop(timing, "qcom,mdss-dsi-" + k))[0] for k in keys if prop(timing, "qcom,mdss-dsi-" + k)})
    print("# dsc", {k: u32s(prop(timing, "qcom,mdss-dsc-" + k)) for k in
                    ["slice-height", "slice-width", "slice-per-pkt", "bit-per-component", "bit-per-pixel", "encoders", "version", "scr-version"]})
    for p in props:
        v = prop(timing, p)
        print("# %s (state %s)" % (p, prop(timing, p + "-state")))
        if not v:
            print("  <none>")
            continue
        for dtype, last, wait, payload in decode(v):
            print("  %-18s last=%d wait=%-3d %s" % (DTYPES.get(dtype, hex(dtype)), last, wait,
                                                    " ".join("%02x" % x for x in payload)))


if __name__ == "__main__":
    main()
