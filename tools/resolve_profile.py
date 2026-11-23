#!/usr/bin/env python3
"""Resolve a profile.txt written by WWE2K16_PROFILE (src/profile_sampler.cpp) with the linker map.

    python -I tools/resolve_profile.py profile.txt out/build/v10/wwe2k16_native.map [top]

Lines "count module+offset thread" are grouped by function (offsets in wwe2k16_native.exe come from the map: the nearest
symbol at or below the offset) and by thread; the top entries are printed with their share of all samples.
"""
import bisect
import re
import sys
from collections import Counter

prof, mapfile = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
base = 0x140000000
pat = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s")
best = {}
for line in open(mapfile, encoding="latin1"):
    m = pat.match(line)
    if not m:
        continue
    addr, name = int(m.group(2), 16) - base, m.group(1)
    if addr < 0:
        continue
    # prefer the plain sub_XXXX name over its __imp__ alias at the same address
    if addr not in best or (best[addr].startswith("__imp__") and not name.startswith("__imp__")):
        best[addr] = name
addrs = sorted(best)


def resolve(off):
    i = bisect.bisect_right(addrs, off) - 1
    return best[addrs[i]] if i >= 0 else "?"


total = 0
by_func, by_thread, by_mod = Counter(), Counter(), Counter()
for line in open(prof):
    count, where, thread = line.split(" ", 2)
    count = int(count)
    mod, off = where.rsplit("+", 1)
    off = int(off, 16)
    total += count
    by_thread[thread.strip().rsplit("#", 1)[0]] += count
    by_mod[mod] += count
    by_func[(resolve(off) + (" (waiting)" if "waiting-in" in mod else "")) if mod.lower().startswith("wwe2k16") else f"[{mod}]"] += count
print(f"{total} samples")
print("-- by module")
for m, c in by_mod.most_common(8):
    print(f"{c:7d} {100 * c / total:5.1f}%  {m}")
print("-- by thread")
for t, c in by_thread.most_common(10):
    print(f"{c:7d} {100 * c / total:5.1f}%  {t}")
print("-- by function")
for f, c in by_func.most_common(top):
    print(f"{c:7d} {100 * c / total:5.1f}%  {f}")
