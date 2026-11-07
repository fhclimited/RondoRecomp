# Distribution: player-supplied game, recompiled on the player's PC

Goal: the release contains no game code or data. The player provides their own copy of the
game as a zip, and a setup program turns it into a native `rondo.exe` on their machine.
This is the same model as LASRecomp: tools are shipped, the game-specific code is generated locally.

## What the player does

1. Download the RondoRecomp release and unpack it anywhere.
2. Run **RondoRecomp Setup** and choose the zip of their *Castlevania: Rondo of Blood* disc image.
3. Setup checks the disc, extracts what it needs, recompiles the game code and produces `rondo.exe`.
4. From then on: double-click `rondo.exe`.

## Release contents (no game material)

| Item | Purpose | License |
|---|---|---|
| `RondoRecomp Setup.exe` | import, check, extract, recompile, build | ours |
| `sdk/` | prebuilt runtime static library + headers (CPU fallback, video, audio, System Card replacement, frontend) | ours |
| `SDL3.dll` (+ deps) | window, input, audio | zlib |
| `zig/` | C compiler and linker used during setup (`zig cc`), Zig 0.17.0, trimmed | MIT; LLVM/Clang/LLD: Apache 2.0 with LLVM exceptions |

## Setup pipeline

1. **Import**: read the zip (CUE with BIN, IMG, ISO and/or WAV tracks). Nothing is written until the disc checks out.
2. **Identify**: PC Engine CD signature on the first data track, then a fingerprint of the boot
   program sector to recognise the release (unknown releases are refused or need explicit confirmation).
   The original Japanese disc and the English translation share the boot sector; the language
   comes from sectors 5-10 of the data track, which the translation changes (`discid_language`).
3. **Extract** to `game/en/` or `game/ja/` by language, keeping a disc already installed in the
   other language (a single disc from Setup up to 0.6, straight in `game/`, is moved into its
   language's folder first).
4. **Recompile**: the generator (built into Setup, no Python needed) walks the boot loader and the
   95 overlay groups from the main loader table, discovers code in every overlay, and emits C into
   `build/gen/`. Code that cannot be proven statically keeps an interpreter fallback.
5. **Build**: `zig cc` compiles `build/gen/*.c` and links it with `sdk/librondo.a` and SDL3 into
   `rondo.exe`. Builds are resumable: generated files that are already compiled are skipped.
6. **Done**: `rondo.exe` next to `game/`; Setup offers to launch it.

An `icon.ico` or `icon.png` (up to 256 x 256) the player puts next to Setup is built into
`rondo.exe` as its icon (resource 1, compiled by `zig cc` from `build/rondo.rc`; a PNG goes
into an .ico as it is). None is shipped: no game artwork is included.

The generated C and the extracted data stay on the player's PC and are never redistributed.

## Current state

All five steps are done.

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang && cmake --build build   # also assembles build/sdk
tools/fetch_zig.sh                                                             # pinned Zig 0.17.0 -> build/zig
tools/package_release.sh 0.2       # dist/RondoRecomp + dist/RondoRecomp-0.2-windows-x64.zip
```

Release folder (412 MB unpacked, 103 MB zipped; Zig is most of it):

| File | |
|---|---|
| `RondoRecomp Setup.exe` | Setup with a window: choose the zip, watch progress, Play / Rebuild |
| `rondo_setup.exe` | the same steps from the command line (`--rebuild` too) |
| `sdk/` | runtime and frontend sources, SDL3 headers and import library, DLLs for rondo.exe |
| `zig/` | C compiler used by Setup |
| `README.txt`, `THIRD_PARTY.txt` | player instructions, third-party licenses |

What Setup does (about 40 s; the first build also prepares Zig's C library):

1. import the zip (CUE + tracks, subfolders allowed, CRC-checked, Zip64 aware)
2. check the disc (PC Engine signature, boot-sector fingerprint, language); stage in `game.tmp/`,
   then `game/en/` or `game/ja/`
3. recompile: run the game headless to find its code, emit C into `build/gen/` (docs/recompiler.md);
   from the English disc when it is installed (its capture run sees the most code), else the
   Japanese one. The other disc runs the same code, checked as it goes (99% native)
4. build: `zig cc` compiles the runtime + generated C into `rondo.exe` (GUI subsystem), copies DLLs

`rondo.exe` uses `game/en/` and `game/ja/` next to itself (the menu's Language switches), keeps
saves in `%APPDATA%\RondoRecomp` (`-ja` files for Japanese), records code it
had to interpret in `misses.txt`, and writes `rondo.log` (share of CPU cycles run natively).
`Rebuild` / `--rebuild` recompiles with those misses.

Tested from the packaged zip unpacked into an empty folder: Setup built rondo.exe from the
zipped disc with no options, and the game ran at 99.9% native CPU cycles.

Possible follow-ups: trim the Zig toolchain like LASRecomp's `tools/zig_slim.sh` does (most of
the download), and an icon and version resource for the executables.

## Work plan

1. ~~Setup core: zip import, identification, extraction.~~ Done.
2. ~~Build orchestration: runtime SDK, `zig cc`, `rondo.exe`.~~ Done.
3. ~~Recompiler~~ Done (see docs/recompiler.md).
4. ~~Setup GUI.~~ Done (`RondoRecomp Setup.exe`).
5. ~~Release packaging.~~ Done (`tools/package_release.sh`).
