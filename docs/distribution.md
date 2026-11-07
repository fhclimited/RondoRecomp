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

1. **Import**: read the zip (CUE with BIN, ISO and/or WAV tracks). Nothing is written until the disc checks out.
2. **Identify**: PC Engine CD signature on the first data track, then a fingerprint of the boot
   program sector to recognise the release (unknown releases are refused or need explicit confirmation).
3. **Extract** to `game/`: the CUE sheet and the track files it references.
4. **Recompile**: the generator (built into Setup, no Python needed) walks the boot loader and the
   95 overlay groups from the main loader table, discovers code in every overlay, and emits C into
   `build/gen/`. Code that cannot be proven statically keeps an interpreter fallback.
5. **Build**: `zig cc` compiles `build/gen/*.c` and links it with `sdk/librondo.a` and SDL3 into
   `rondo.exe`. Builds are resumable: generated files that are already compiled are skipped.
6. **Done**: `rondo.exe` next to `game/`; Setup offers to launch it.

The generated C and the extracted data stay on the player's PC and are never redistributed.

## Current state

Steps 1 and 2 of the work plan are done (command line). From a repository build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang && cmake --build build   # also assembles build/sdk
tools/fetch_zig.sh                                                             # pinned Zig 0.17.0 -> build/zig
build/rondo_setup.exe <disc.zip | disc.cue> --out <install dir> --sdk build/sdk --zig build/zig/zig.exe
```

A release will place `sdk/` and `zig/` next to `rondo_setup.exe`, so those flags are not needed.
Setup writes `<install dir>/rondo.exe`, its DLLs, `game/` (the extracted disc) and `build/`
(generated code and compiler cache). The first build takes about 15 s because Zig prepares its
C library once. `rondo.exe` looks for `game/` next to itself before anything else.

The runtime is compiled from source by Setup (`sdk/sources.txt`) instead of being shipped as a
prebuilt library, so the runtime and the generated code always use the same compiler and C library.
SDL3 is linked through its import library, passed by path.

Checked: the zip may keep the disc in a subfolder; every file a CUE references must be present;
entries are CRC-checked while extracting; non-PC Engine or unknown discs are refused (unknown ones
can be allowed with `--allow-unknown`); nothing replaces `game/` until the new disc passes the check.

## Work plan

1. ~~Setup core (command line first): zip import, identification, extraction.~~ Done.
2. ~~Build orchestration: runtime SDK, `zig cc` invocation, `rondo.exe` output (interpreter only at
   this stage, so the whole flow works end to end).~~ Done.
3. Recompiler: code discovery across all overlays (static analysis plus recorded traces), C emitter,
   runtime dispatch that checks which overlay is loaded, interpreter fallback.
4. Setup GUI (SDL3): file picker, progress, clear error messages.
5. Release packaging: script that assembles the release zip and fetches the pinned Zig toolchain.
