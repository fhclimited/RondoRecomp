# Runtime

The native runtime is written in C11 and built with CMake. It will host recompiled game
code next to an interpreter fallback and the PC Engine hardware (VDC, VCE, PSG, timer,
interrupt controller, CD-ROM, ADPCM) plus high-level replacements for the System Card
calls the game uses.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build
ctest --test-dir build --output-on-failure
```

## CPU interpreter (`runtime/cpu/huc6280.c`)

One instruction per `hu_step()`, against a 21-bit physical bus through MPR0-7.
Returns CPU cycles; speed (CSL/CSH) is reported, not applied: converting to
master-clock time is the system's job.

### Conformance

Validated against [SingleStepTests/huc6280](https://github.com/SingleStepTests/huc6280)
(MIT, generated from Mesen; 2,500 tests per opcode). Put the v1 `*.json.bin` files in
`work/sst/v1`, or set `-DSST_DIR=...`.

Result: **253/256 opcodes pass every test; 639,592/640,000 tests overall (99.94%).**
Registers, flags, MPRs, all recorded RAM and total cycle counts are checked.

Behaviours the tests pinned down:

- ST0/ST1/ST2 take 4 cycles
- TSB/TRB set N and V from the operand before modification
- PLP/RTI never leave B set in P (T is restored from the stack)
- TMA with several mask bits returns the OR of the selected MPRs
- Block transfers push Y, A, X *before* fetching their six operand bytes
- T-mode applies to ORA/AND/EOR/ADC (not SBC), +3 cycles; decimal ADC/SBC +1 cycle

Known differences: TII (73), TDD (C3) and TAI (F3), about 400 tests in total. In every case
a transfer of up to 64 KiB wraps around the address space and overwrites its own
source or the stack mid-copy. The suite records only 500 RAM pairs and 500 bus
cycles, so the generator state needed to model these cases is not available.
Game code does not do this. The test harness also treats bytes copied from
unrecorded addresses as unknown, because the generator's background memory is random.

Not covered by the suite and still to do: interrupt timing (including the one-instruction
delay after CLI/SEI/PLP), VDC/VCE access wait states, and the timer and interrupt controller,
which belong to the system layer.

## System layer (`runtime/pce/`)

| File | Contents |
|---|---|
| `pce.c` | memory map (Super RAM `$68-$7F`, CD RAM `$80-$87`, BRAM `$F7`, WRAM `$F8-$FB`, I/O `$FF`), interrupt controller, timer, joypad, scanline-timed frame loop (263 lines × 1365 master clocks) |
| `video.c` | VCE palette; VDC registers, VRAM, VRAM/SATB DMA, raster/vblank interrupts, background + sprite scanline renderer |
| `disc.c` | CUE + ISO/BIN/WAV reader addressed by absolute LBA |
| `bios_hle.c` | System Card functions in C, run when the CPU reaches a jump-table address |

Bank `$00` holds a small ROM of our own: the interrupt stubs follow the System Card's dispatch
rule (bit set in `irq_m` → jump through the work-RAM vector), plus the two bank-number bytes
the boot code reads. No System Card image is needed.

System Card functions implemented: CD_BASE, CD_READ (all modes), CD_STAT, CD_PLAY, CD_SEARCH,
CD_PAUSE, CD_FADE, AD_TRANS, EX_GETVER, EX_SETVEC, EX_JOYSNS, EX_SCRMOD, EX_SCRSIZ, EX_DOTMOD,
EX_MEMOPEN, MA_MUL8U/8S/16U, MA_DIV16U/16S.
Stubbed for now (return success): AD_PLAY/CPLAY/STOP/STAT (ADPCM voice), BM_* (save data is
not kept), EX_SATCLR. Not yet implemented: EX_GETFNT (font glyphs). PSG (sound effects) is not
emulated yet.

CD audio: CD_PLAY/CD_SEARCH positions are BCD MSF (`$40`), LBA (`$00`) or track number (`$80`);
the low bits of the end-type byte select repeat (1) or play once. `pce_audio_render()` streams the
WAV/BIN audio tracks at 44.1 kHz with fades.

## Windowed frontend (`tools/frontend`, needs SDL3)

```sh
build/rondo.exe "<path>/game.cue"
```

| Input | Keyboard | Gamepad |
|---|---|---|
| D-pad | arrows | d-pad / left stick |
| I | X | East (B) |
| II | Z | South (A) |
| RUN | Enter | Start |
| SELECT | Right Shift / Backspace | Back |
| Fullscreen / quit | F11 / Esc | |

Video is shown at 4:3 with nearest-neighbour scaling; frames are paced at 59.83 Hz.
The build copies `SDL3.dll` and `libiconv-2.dll` next to the executable.

### Headless runner

```sh
build/rondo_headless <game.cue> --frames 3000 --shot-every 150 --out work/shots \
    [--log-bios | --log-bios-all] [--press FRAME:run,i:LEN] [--profile] \
    [--watch BANK:ADDR] [--trace FRAME:COUNT] [--bios-reset syscard3.pce]
```

The interpreter runs at about 2,600 frames per second (~43× real time).

Progress so far: boot, publisher logo, the full intro, attract mode, title, and the save-file
menu all render and run; menu input works. `--wav FILE` records the audio output.

### Hardware behaviour this depends on

- **VDC status flags are only latched when their interrupt is enabled.** Rondo's vblank
  handler checks the raster flag first and takes a raster-only path when it is set. If a raster
  match is latched while raster interrupts are disabled, it arrives together with vblank and the
  frame logic never runs again (the game froze on the logo before this was fixed).
- Rondo never uses the timer: its timer handler is a bare `RTI`. The System Card leaves the
  timer stopped after reset (checked by running the original reset code with `--bios-reset`).
- Input arrives through the System Card's `EX_JOYSNS` variables (`$2228` held, `$222D` newly
  pressed, `$2232` previous), which the game refreshes every frame.
