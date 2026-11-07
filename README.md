# RondoRecomp

Work in progress: static recompilation of *Castlevania: Rondo of Blood* (PC Engine Super CD-ROM²)
into a native PC application.

**No game data is included or committed.** Tools read your own disc image (CUE sheet with
ISO/WAV/BIN tracks); everything derived from it goes to `work/`, which is git-ignored.

## Analysis tools

Requires Python 3.10+.

```sh
python tools/survey_disc.py   <game.cue>   # track layout, IPL header, data-track comparison
python tools/explore_boot.py  <game.cue>   # boot program: BIOS calls, loader → work/boot.asm
python tools/list_overlays.py <game.cue>   # every overlay group the main loader can load
python tools/explore_main.py  <game.cue>   # resident code map (bank-aware) → work/main.asm
```

Findings so far: [docs/findings.md](docs/findings.md).

## Runtime

C11 + CMake. So far: a HuC6280 interpreter that passes 253/256 opcodes of the
SingleStepTests suite (99.94% of 640,000 tests), plus the PC Engine hardware and System Card
functions needed to run the game from boot through the intro, title and save menu, with
CD audio. `build/rondo.exe <game.cue>` opens it in a window (SDL3). Game code still runs on
the interpreter; static recompilation comes next. Details: [docs/runtime.md](docs/runtime.md).

## Setup (player flow)

`rondo_setup` takes the player's own zipped disc image, checks it, extracts it and builds
`rondo.exe` on their machine with a bundled Zig C compiler. See
[docs/distribution.md](docs/distribution.md).

## Layout

- `runtime/cpu/` HuC6280 interpreter
- `tests/` conformance test runners
- `tools/pce/` platform code: disc access, HuC6280 decoder, System Card table, banked code explorer
- `tools/rondo/` game-specific formats (overlay loader)
- `tools/setup/` Setup: zip import, disc check, code generation, build with zig cc
- `tools/frontend/` windowed frontend (SDL3)
- `docs/` notes
- `work/` local output and test data (ignored)
