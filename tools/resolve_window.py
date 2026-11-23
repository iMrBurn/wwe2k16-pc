#!/usr/bin/env python3
"""CPU use by time window and the hottest functions inside a window, from a profile with time buckets.

    python -I tools/resolve_window.py profile.txt out/build/v10/wwe2k16_native.map [from_s to_s] [top]

Profile lines "chain:running@12:..." / "chain:waiting@12:..." carry the second (since the sampling started) of the sample.
Without a window it prints, per second, the share of samples that ran in game code (all threads) and in the busiest thread;
with a window it prints the busiest threads and the hottest leaf functions there.
"""
import bisect
import re
import sys
from collections import Counter, defaultdict

prof, mapfile = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
win = (float(args[0]), float(args[1])) if len(args) >= 2 else None
top = int(args[2]) if len(args) >= 3 else 15
base = 0x140000000
pat = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s")
best = {}
for line in open(mapfile, encoding="latin1"):
    m = pat.match(line)
    if m:
        a = int(m.group(2), 16) - base
        if a >= 0 and (a not in best or best[a].startswith("__imp__")):
            best[a] = m.group(1)
addrs = sorted(best)


def res(o):
    i = bisect.bisect_right(addrs, o) - 1
    return best[addrs[i]].replace("__imp__", "") if i >= 0 else "?"


per_sec = defaultdict(lambda: Counter())      # second -> {running, waiting}
per_sec_thread = defaultdict(Counter)         # (second, thread) -> running samples
leaf = Counter()
thread_run = Counter()
chains = defaultdict(Counter)
for line in open(prof):
    c, w, t = line.split(" ", 2)
    if not w.startswith("chain:"):
        continue
    kind, frames = w[6:].split(":", 1)
    state, sec = kind.split("@")
    sec = int(sec)
    c = int(c)
    t = t.strip().rsplit("#", 1)[0]
    per_sec[sec][state] += c
    if state == "running":
        per_sec_thread[sec][t] += c
    if win and win[0] <= sec < win[1] and state == "running":
        names = [res(int(f, 16)) for f in frames.split(">")]
        leaf[names[0]] += c
        thread_run[t] += c
        chains[t][" < ".join(names[:5])] += c
if not win:
    for sec in sorted(per_sec):
        r, w = per_sec[sec]["running"], per_sec[sec]["waiting"]
        busiest = per_sec_thread[sec].most_common(1)
        bt = f"{busiest[0][0]} {busiest[0][1]}" if busiest else "-"
        print(f"{sec:4d}s  running {r:6d} waiting {w:6d}  busiest: {bt}")
else:
    total = sum(leaf.values()) or 1
    print(f"window {win[0]}-{win[1]} s, {total} running samples")
    for t, c in thread_run.most_common(5):
        print(f"  {t}: {c}")
        for ch, n in chains[t].most_common(4):
            print(f"      {100 * n / total:5.1f}%  {ch}")
    print("-- hottest leaf functions")
    for n, c in leaf.most_common(top):
        print(f"  {100 * c / total:5.1f}%  {n}")
