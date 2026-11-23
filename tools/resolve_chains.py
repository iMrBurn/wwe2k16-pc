#!/usr/bin/env python3
"""Show where each game thread spends its time, from the "chain:" lines of a profile (src/profile_sampler.cpp).

    python -I tools/resolve_chains.py profile.txt out/build/v10/wwe2k16_native.map [top]

For every thread, the most frequent call chains (innermost game function first), names from the linker map, with the share of
the thread's samples that were "running" (stopped inside game code) or "waiting" (in a kernel wait called from game code).
"""
import bisect
import re
import sys
from collections import Counter, defaultdict

prof, mapfile = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 4
base = 0x140000000
pat = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s")
best = {}
for line in open(mapfile, encoding="latin1"):
    m = pat.match(line)
    if not m:
        continue
    addr, name = int(m.group(2), 16) - base, m.group(1)
    if addr >= 0 and (addr not in best or (best[addr].startswith("__imp__") and not name.startswith("__imp__"))):
        best[addr] = name
addrs = sorted(best)


def resolve(off):
    i = bisect.bisect_right(addrs, off) - 1
    return best[addrs[i]] if i >= 0 else "?"


per_thread = defaultdict(Counter)
totals = Counter()
state = defaultdict(Counter)
for line in open(prof):
    count, where, thread = line.split(" ", 2)
    if not where.startswith("chain:"):
        continue
    kind, frames = where[6:].split(":", 1)
    kind = kind.split("@")[0]
    thread = thread.strip()
    names = [resolve(int(f, 16)).replace("__imp__", "") for f in frames.split(">")]
    # collapse the wait wrappers' own frames and repeats
    per_thread[thread][(kind, " < ".join(names[:6]))] += int(count)
    totals[thread] += int(count)
    state[thread][kind] += int(count)
for thread, total in totals.most_common(12):
    w, r = state[thread]["waiting"], state[thread]["running"]
    print(f"=== {thread}: {total} samples, waiting {100 * w / total:.0f}%, running in game code {100 * r / total:.0f}%")
    for (kind, chain), c in per_thread[thread].most_common(top):
        print(f"  {100 * c / total:5.1f}% {kind:8s} {chain}")
