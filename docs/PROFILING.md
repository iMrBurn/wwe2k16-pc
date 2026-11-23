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
