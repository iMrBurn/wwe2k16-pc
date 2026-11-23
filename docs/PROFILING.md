# Profiling the game (where the time goes)

A sampling profiler is built into `wwe2k16_native.exe`, off unless asked. It stops every thread for an instant, many times a
second, reads where it is and, for a thread waiting in the kernel, walks up its stack to the game function that called the wait.

```
set WWE2K16_PROFILE=40             seconds to sample
set WWE2K16_PROFILE_DELAY=25       seconds to wait first (default 0.3)
set WWE2K16_PROFILE_INTERVAL_MS=10 pause between sweeps (default 1; 5-10 keeps the game close to normal speed)
set WWE2K16_PROFILE_FILE=profile.txt
```

Then, with the linker map of the same build (`out\build\v10\wwe2k16_native.map`):

```
python -I tools\resolve_profile.py profile.txt out\build\v10\wwe2k16_native.map      by module, thread and function
python -I tools\resolve_chains.py  profile.txt out\build\v10\wwe2k16_native.map      per thread: call chains, waiting vs running
```

Function names are the recompiled guest addresses (`sub_82C30CA0`); `tools/asmindex.py` and `work/asm_tu.json` give their code.
Do not sample with an interval of 1 ms for long: the game then runs far from normal speed.

## What it showed (2026-11-23, RTX 5070 laptop, scripted run: title, menus, a One on One match)

- **Boot (first 14 s):** 94 % of all samples are threads waiting; game code runs for under 1 % of them. The start-up is not
  limited by the CPU; see `docs/STARTUP_SEQUENCE.md`.
- **Menus and a match (30 to 75 s):** one thread, the game and render thread (`XThread...F8000064`), runs 19 to 60 % of the time;
  every other thread is idle (Wwise sound, file I/O, workers: 99 to 100 % waiting).
- In that thread, **most of the running time is in `sub_82C30CA0` and `sub_82C34770`**, called from the recompiled D3D
  library (`sub_82C32510`) that the game's draw calls go through: `sub_82C30CA0` is a loop of `db16cyc` (Xbox 360 "wait 16
  cycles") instructions around a check of the command ring buffer, i.e. it waits for the GPU to take commands. With the native
  renderer the guest GPU is a null GPU (synchronisation only), so this is waiting, not work, and it ends when the null GPU
  moves on (it follows the 60 Hz frame pacing). The real per-frame work is spread thin (no other function above 1 %).
- The rest of the render thread's time is the renderer: constant uploads (`UploadPixelShaderConstants`,
  `UploadVertexShaderConstants`, about 7 %) and texture mirrors (`Build2DMirror`, about 1 %).

## Match loading: what the profile shows and does not show (second session of notes)

`tools/resolve_window.py` prints the CPU use second by second (profile lines carry a time bucket) and the hottest functions of a
window. `WWE2K16_TEST_INPUT` now logs each button the script presses (`[test-input] buttons 0x1000` = A), so a profile can be
lined up with the script.

- A full scripted run (title, menus, match select, first entrance; 100 s, sampled every 10 ms) shows **no CPU burst at any
  time**: running samples are 24 to 47 per second all along, in one thread (the game and render thread). Nothing looks like a
  load phase: the loading screens are waiting, not computing, in this build.
- What does show up are **single long stalls of the game and render thread**, found in the `[perf]` log lines (max interval):
  - 10000.5 ms at about 52 s of a scripted run (`fps30-0.log`, 2026-11-19), exactly 10 s: it looks like a wait that ran out its
    timeout, not work; it did not repeat in later runs;
  - 1036 ms (`native-run3.log`, 2026-11-12) and 1604 ms (a test-pack run, 2026-11-12), at menu and match transitions;
  - 5234 ms (`profm-90.log`, a run slowed by the sampler).
- These stalls are intermittent and were not reproduced under the sampler, so they have not been traced to a wait yet. To
  catch one: run unsampled, note the time of the stall from the log, then sample a window around the same time with a long
  interval (25 ms), or add a watchdog that dumps the chains of all threads when the render thread has not presented for 800 ms.
