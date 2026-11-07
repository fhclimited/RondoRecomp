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

## Status

- **Static recompiler:** Setup recompiles the game's code from the player's disc into C and
  builds a native `rondo.exe` with a bundled Zig compiler. On a 20,000-frame run through
  Stage 0, 99.3% of CPU cycles run natively; 402,593 native chunk executions were checked
  against the interpreter with no differences. See [docs/recompiler.md](docs/recompiler.md).
- **Runtime:** HuC6280 interpreter (253/256 opcodes pass every SingleStepTests case), PC Engine
  video, sound (PSG effects, ADPCM voices, CD audio), backup RAM saves, and the System Card
  functions the game uses, reimplemented in C (no BIOS needed). See [docs/runtime.md](docs/runtime.md).

## For players

Download the release, run `RondoRecomp Setup.exe`, choose a zip of your own disc image, then
click Play. See [docs/PLAYER_README.txt](docs/PLAYER_README.txt) and
[docs/distribution.md](docs/distribution.md).

## Layout

- `runtime/cpu/` HuC6280 interpreter and shared instruction semantics
- `runtime/pce/` PC Engine system, video, disc, backup RAM, System Card replacement
- `runtime/recomp/` native-code dispatch and verification
- `tests/` conformance test runners
- `tools/pce/` platform code: disc access, HuC6280 decoder, System Card table, banked code explorer
- `tools/rondo/` game-specific formats (overlay loader)
- `tools/setup/` Setup (command line and window): zip import, disc check, recompile, build with zig cc
- `tools/recomp/` code generator and capture script
- `tools/frontend/` windowed frontend (SDL3)
- `docs/` notes
- `work/` local output and test data (ignored)
