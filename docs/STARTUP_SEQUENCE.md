# Start-up sequence of WWE 2K16 (title update 3), what was found

Notes from trying to skip the start-up screens (logo, copyright text, "Please do not try this at home") on 2026-11-23. Nothing
of it is in the code: skipping them made the game stall on a black screen. Kept for the next attempt.

## The state machine

`CStartupCtrlSequence` (constructor `sub_827C1920`, vtable 0x82033A80, run loop `sub_827C1A30`, screen creation `sub_827C1840`):

- the current state number is the word at **0x837739DC**; a table of 16-byte entries at **0x83417290** gives, per state: the
  factory of the screen (+0), the next state on success (+4), the next state otherwise (+8), a callback (+12);
- the screen of the current state is the child at *sequence + 64*; the run loop calls its virtual functions (+36: update, +48:
  succeeded?) and moves to the next state when it is done; a screen is done when its phase (*child + 16*) is 5;
- one byte per state at **0x83773998** says "this screen was shown already": the screen constructors take a short path
  (`sub_823A2400`, `sub_823A2460`: phase 3 then 5) when it is set, so coming back to the title from the menus does not repeat
  them. The constructor of the sequence clears these bytes (`memset`) before the first screen is created.

Observed boot (about 15 s with the state trace; the title is reached at 11 to 15 s):

| state | what the player sees | seen at |
|---|---|---|
| 0, 1 | logo (1.2 s each; state 1 is also a check: forcing it done sends the sequence to state 7, black screen) | 0 s, 1.2 s |
| 3 | copyright text | 2.4 s |
| 4 | "Please do not try this at home" | 8 s |
| 5 | title, PRESS START BUTTON | 15 s |

## What did not work (all stall on a black screen with the WWE loading icon, forever)

- setting the "shown" bytes of states 3 and 4 after the sequence constructor (state 3 ends at once, then state 4 never shows
  and the title never comes);
- forcing the phase of the screens of states 3 and 4 to 5 from the run loop;
- cutting the countdown of the safety screen (object + 128, sub_827EBD88).
- Not working either, for the timing: START taps from the start (these screens ignore the input), an uncapped frame rate,
  the guest clock at 8x (the screens did not end sooner).

So these screens are also the game's loading screens: their constructors start loading that the title needs. Skipping the
screens without that loading leaves the title without its data. The way forward is a faster load (the screens then end sooner by
themselves), or a skip that keeps the loading calls and only hides the pictures.

## Dev aids used (not kept)

- `WWE2K16_DUMP_IMAGE=<file>`: write guest memory 0x82000000-0x838C0000 5 s after the start (a patched image to search for
  strings; the strings found here: `CStartupCtrlSequence`, `GAME:\movies\wwe16_start_logo.bk2`, `_startup.pac`).
- A thread polling 0x837739DC every 5 ms and logging each change.
