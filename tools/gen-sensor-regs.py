#!/usr/bin/env python3
"""Turn one register list from chromatix-regs.py output into C.

usage: gen-sensor-regs.py <regs.txt> <list-index> [skip-addr,...]

<list-index> counts the '==' lists in file order (0-based). Prints
cci_reg_sequence entries, four per line; addresses in skip are left out.
"""
import re
import sys


def main():
    lists = []
    for line in open(sys.argv[1]):
        if line.startswith("=="):
            lists.append([])
            continue
        m = re.match(r"\s+0x([0-9a-f]+) = (0x[0-9a-f]+)\s+\(a[12] d1 op0\)", line)
        if m and lists:
            lists[-1].append((int(m.group(1), 16), int(m.group(2), 16)))
    skip = {int(a, 16) for a in sys.argv[3].split(",")} if len(sys.argv) > 3 else set()
    regs = [r for r in lists[int(sys.argv[2])] if r[0] not in skip]
    for a, v in regs:
        print("\t{ CCI_REG8(0x%04x), 0x%02x }," % (a, v))


if __name__ == "__main__":
    main()
