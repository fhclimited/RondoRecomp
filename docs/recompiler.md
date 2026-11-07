# Recompiler design

The recompiler turns the game's HuC6280 code into C on the player's machine (inside Setup).
The runtime runs that C natively and falls back to the interpreter for anything not covered.

## Correctness by construction

All instruction semantics live in `runtime/cpu/huc6280_ops.h` as inline functions. The
interpreter (`huc6280.c`, validated by SingleStepTests) and the generated code call the same
functions, so generated code differs from the interpreter only in operand decoding and control
flow, which the generator computes statically.

## Regions

A region is one C function compiled from an entry point `(bank, address)`. It contains every
instruction reachable from the entry through fallthrough, conditional branches and `JMP`/`BRA`
inside the same 8 KiB window, up to a size limit. Branches inside the region are C `goto`s, so
loops run natively. A region exits (sets `PC`, returns the cycles used) at:

- `JSR`/`BSR` (return address pushed), `RTS`, `RTI`, `BRK`, indirect jumps, targets outside the region
- `TAM` (the code's own window may be remapped), block transfers (may overwrite code)
- `CLI`/`SEI`/`PLP` (interrupt state changes take effect at once), `CSL`/`CSH` (clock speed)
- an instruction that would cross the window boundary
- a backward branch when the cycle budget is used up (raster timing, polling loops)

Inside a region, `SET` is handled statically: the next instruction is emitted in T mode.
The dispatcher only enters a region with T clear and no interrupt pending.

## Overlays and self-checking

Each region stores the code bytes it was compiled from (as byte ranges). Before running it, the
runtime compares them with memory; a per-bank write counter (`page_gen`) caches the result until
the bank is written again. Different overlay contents at the same address are different regions.
Anything without a matching region runs on the interpreter.

## Where the code comes from

Setup boots the game headless on the interpreter and calls the game's own loader (`6A:6FD8`) once
per overlay group, snapshotting RAM banks after each. This reproduces every overlay exactly as the
game builds it, including post-processing. Entry points come from:

1. a seed list (`data/seeds.txt`: bank, address, hash of the first bytes; no game code), recorded
   from interpreter runs with `rondo_headless --record-seeds`
2. static expansion: branch, jump and call targets found while compiling regions
3. the player's own misses (`misses.txt`, written by `rondo.exe`), picked up by the next Setup run

## Verification

`rondo_headless --verify N` re-runs every Nth region on the interpreter from the same starting
state and compares CPU registers, RAM, VDC/VCE state and I/O state. Coverage statistics report
the share of CPU cycles executed natively.
