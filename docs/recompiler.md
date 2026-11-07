# Recompiler

The recompiler turns the game's HuC6280 code into C on the player's machine (inside Setup).
The runtime runs that C natively and falls back to the interpreter for anything not covered.

## Correctness by construction

All instruction semantics live in `runtime/cpu/huc6280_ops.h` as inline functions. The
interpreter (`huc6280.c`, validated by SingleStepTests) and the generated code call the same
functions, so generated code differs from the interpreter only in operand decoding and control
flow, which the generator computes statically.

## Finding the code (`tools/recomp/gen.c`)

Setup runs the game itself to find its code, from the player's own disc:

1. **Capture run.** The game boots headless on the interpreter and plays a fixed input script
   (`tools/recomp/capture_script.h`): intro, attract sequence, title, save menu, name entry and
   Stage 0 gameplay. Every dispatch point (the instruction after a branch, jump, call, return,
   interrupt, BIOS call, bank switch, ...) is recorded together with a snapshot of the bank the
   code runs from. This includes code the game assembles or patches at runtime.
2. **Overlay loader passes.** The game's own loader (`6A:6FD8`) is called for all 95 overlay
   groups, from the post-boot state and cumulatively, to add every overlay image.
3. **Extra entry points** from `misses.txt` (entry points `rondo.exe` had to interpret while the
   player played) are matched against those images by basic-block hash.

## Chunks

All code reachable (fallthrough, branches, jumps) from the entry points in one bank image and
one 8 KiB window becomes one **chunk**, compiled as one C function that starts with a `switch`
on the PC. Every instruction of a chunk is a valid entry. Snapshots that agree on the chunk's
code bytes share one chunk, so data-only differences between snapshots do not duplicate code.

Each chunk is split into **components**: instructions connected by fallthrough, branches and
jumps. Execution entering a component stays inside it until it leaves the chunk, so each
component is checked against memory on its own. This matters for work banks such as `$6C`,
whose other contents change while a routine in it stays the same.

Inside a chunk, branches are `goto`s, so loops run natively. A chunk exits (sets `PC`, returns
the cycles used) at calls, returns, indirect jumps, `TAM`, block transfers, `CLI`/`SEI`/`PLP`,
`CSL`/`CSH`, the window edge, or a backward branch once the cycle budget for the scanline is
used. `SET` (T mode) is handled statically for the next instruction.

## Self-modifying code

Rondo patches some of its own instructions. The generator handles this in three ways:

- **Patched operands.** Entries captured with several variants are compared over their code;
  bytes that differ only as operands (including `JMP`/`JSR` targets) are marked as patched for
  that bank window. Constant-address stores into the code's own operands are found statically too.
  Generated code reads patched operands from memory at run time and leaves them out of the code
  check, so one chunk covers every variant.
- **Code check.** Each chunk stores a 64-bit hash of its code byte ranges. Before running, the
  runtime compares it with memory; a per-bank write counter (`page_gen`) caches the result.
- **Writes into running code.** A store that may hit the chunk's own (non-patched) code leaves
  the chunk, so the dispatcher re-checks before continuing.

Anything without a matching chunk runs on the interpreter and is recorded in `misses.txt`.

## Verification

`rondo_headless --verify N` re-runs every Nth chunk call on the interpreter from the same
starting state and compares the whole machine: CPU, all RAM, VDC/VCE, I/O. `--script` replays
the capture input. Results on this disc (20,000 frames, through Stage 0 gameplay):

| | |
|---|---|
| entry points found by the capture run | 10,354 |
| chunks / components / instructions / generated C | 31 / 1,580 / 17,124 / 2.4 MB |
| CPU cycles run natively | 99.33% (99.9% in the intro) |
| chunk executions verified against the interpreter (incl. sound state) | 402,593, 0 differences |

## Tools

```sh
build/rondo_gen <game.cue> --out work/gen [--seeds FILE]      # generate C (what Setup does)
cmake -S . -B build -DRONDO_GEN_DIR=$PWD/work/gen && cmake --build build --target rondo_headless_rc
build/rondo_headless_rc <game.cue> --frames 20000 --script --verify 25 [--misses FILE] [--profile]
build/rondo_headless <game.cue> --record-seeds FILE       # interpreter-only entry point recording
```
