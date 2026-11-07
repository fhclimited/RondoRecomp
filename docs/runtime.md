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
BM_FORMAT/FREE/READ/WRITE/DELETE/FILES: backup RAM in the System Card's file format (`bram.c`),
saved by the frontend in the user's pref folder.
AD_PLAY/STOP/STAT/READ/WRITE/RESET drive the ADPCM player in `sound.c`.
AD_CPLAY streams ADPCM straight from the CD. Stubbed (return success): EX_SATCLR.
Not yet implemented: EX_GETFNT (font glyphs).

## Sound (`runtime/pce/sound.c`)

- **PSG** (sound effects): 6 wavetable channels (32 x 5-bit samples), DDA mode, noise on
  channels 4 and 5, 5-bit volume in 1.5 dB steps and 4-bit channel/global balance in 3 dB steps.
  LFO: channel 1's waveform, clocked `lfo_freq` times slower, modulates channel 0's period
  (x1, x16, x256) and is not heard itself.
- **ADPCM** (voices): OKI MSM5205-style 4-bit ADPCM from the 64 KiB ADPCM RAM at
  32 kHz / (16 - rate), **low nibble first** (checked on the game's own clips: the other order
  decodes to noise), linearly interpolated to 44.1 kHz. AD_STAT reports busy while a clip
  plays; the game waits on that, so it affects game timing.
- PSG writes and ADPCM starts are time-stamped with the master clock: the sound module renders
  up to that moment before applying them. CD audio is mixed in at output (`pce_audio_render`).
- `rondo_headless --wav FILE [--mute-cd]` records the output; the summary reports PSG writes and
  how long PSG and ADPCM were audible.

CD audio: CD_PLAY/CD_SEARCH positions are BCD MSF (`$40`), LBA (`$00`) or track number (`$80`);
the low bits of the end-type byte select repeat (1) or play once. `pce_audio_render()` streams the
WAV/BIN audio tracks at 44.1 kHz with fades.

## Save states and rewind (`runtime/pce/state.c`)

- A snapshot is the whole emulated machine (`Pce`) with host-side fields zeroed: disc handles,
  memory-map pointers, framebuffer, audio output queue, diagnostics, input. Restoring keeps the
  live host fields and invalidates the recompiler's cached code checks (they are keyed by the
  per-bank write counters a restore rolls back).
- Save-state files: header (format, state size, disc fingerprint) + the snapshot, delta-coded
  against zeros (about 380 KB). States from another disc or another build are refused.
- Rewind: after each frame, the XOR difference to the previous snapshot is stored as zero-run /
  literal-run tokens (about 0.8 KB per frame) with that frame's input. XOR is its own inverse, so
  applying the newest difference steps the snapshot back exactly one frame. While rewinding, the
  frontend restores each earlier state and replays one frame of it to show the picture, with
  audio muted; on release play continues from the exact rewound state.
- `build/state_test <game.cue>`: restore + replay and state files reproduce the machine state
  byte for byte; rewinding N frames lands exactly on the recorded state.
- `rondo.exe --autotest` saves, loads and rewinds by itself and logs the result to rondo.log.

## Windowed frontend (`tools/frontend`, needs SDL3)

```sh
build/rondo.exe "<path>/game.cue"
```

| Action | Gamepad | Keyboard | Sends the game |
|---|---|---|---|
| Move | d-pad / left stick | arrows | d-pad |
| Jump | A (South) | X | I |
| Attack | X (West) | Z | II |
| Sub-weapon | B (East) | C | Up + II (macro) |
| Item Crash | Y (North) | V | SELECT |
| Backflip | RB | A | I, release, I (macro) |
| Run (hold) | LB | S | (frontend: speeds the walk up, `game_apply_run`) |
| Pause | Start | Enter | RUN |
| Menu | Back | F1 / Esc | (frontend) |
| Rewind (hold) | LT | R | (frontend) |
| Save / load state, slot | | F5 / F8, F6 / F7 | (frontend) |
| Fullscreen | | F11 | (frontend) |

Macros: the game ignores presses shorter than 3 frames, so each macro step lasts 4 frames;
the backflip is the second I press while still rising (2-8 frames after the first press
ends works, 14 is too late; measured with `rondo_headless --press`).

Modules: `input.c` (mapping, macros, menu navigation), `menu.c` (pages, persisted to
`settings.ini` by `settings.c`), `video.c` (placement and filtering), `game.c` (what the
frontend knows about the game: main state, cheats, run), `wide.c` (widescreen); the last two
are also linked into `rondo_headless`.

Video: aspect 4:3 / square pixels / stretch, fit or whole multiples, filters Sharp (nearest
upscale to a whole factor into a render target, then linear to the final size), Nearest and
Smooth. On wide screens the sides show the picture blurred (downscaled to 16x14, scaled up
linearly, dimmed). The window is high-DPI aware, so fullscreen renders at the display's
native resolution (e.g. 3840x2160). Frames are paced at 59.83 Hz.

Cinematic skip: story scenes run in main state `$0A` (RUN does not skip them in the game).
Skipping runs the emulation at full speed, silently, until the state changes (the 3,566-frame
Stage 0 scene takes under a second), then records one rewind step across the whole scene.

Cheats (`game.c`): health losses are undone while playing (state `$05`); hearts are kept at
99 while playing, unless they drop more than 12 times a second (a tally), then left alone
until 3 quiet seconds pass or the state changes; lives losses are undone outside the menus.
Maria unlocked sets bit 0 of `$1F40` (the current save record) while playing; Play as Maria
sets the character byte `$9F` to 1 whenever a stage may be loading (outside gameplay and
while a stage sets up), so every stage loads Maria's code, the Prologue included.

Run (`game_apply_run`): Rondo sets the player's X velocity once when a walk starts (±$0140
for Richter, ±$0160 for Maria; Richter's jumps ±$0150). While the run button is held, those
exact values in the facing direction are scaled by 2.3 / 1.25 (SotN's run against its walk,
which equals Rondo's), and the walk animation's frame timer gets an extra step on 5 frames
of 6. The game's own movement code then handles walls, stairs and the camera.

Widescreen (`wide.c`, `runtime/pce/video.c`): the VDC renders `vdc.wide` extra pixels on
each side of a 256-pixel picture (background from the 512-pixel map, sprites with 10-bit X
past 512 read as negative); `video.c` keeps the 4:3 pixel shape for the wider picture. The
game is changed by byte patches, each checked against the original bytes (code is loaded
from the disc as the game goes): background columns are streamed 112 px out instead of 64
(and drawn at both distances, because some layers start drawn to 64 only), the room start
draws all 16 columns, rows drawn while scrolling vertically cover the whole map, objects
spawn 128 px out instead of 64, keep moving to 128 px and are removed past 192 px, and the
sprite builder lets pieces through from -112 to 256 + 112. Generators fixed to the screen at
the old spawn edge are moved to the new one; the HUD object moves to the left edge. The
world changes are decided as each room starts (a dispatch hook on the room-start routines):
on for rooms whose camera limits allow sideways scrolling, off for all others, whose
background map may be shared between layers (the Stage 0 carriage chase is drawn wide with
no changes: its scenery layers loop round the whole map). Areas beyond a room's walls are
drawn black (`vdc.wide_x0/x1`). The sprite changes follow each frame's wide/4:3 choice.

Test modes: `rondo.exe --autotest` (save, load, rewind) and `rondo.exe --autotest-features
DIR` (plays the capture script with automatic skipping and all cheats, uncapped; writes
screenshots of the video modes and menu pages to DIR). Both log to `rondo.log` and keep
their files in a `test` folder, never touching the player's saves.
The build copies `SDL3.dll` and `libiconv-2.dll` next to the executable.

### Headless runner

```sh
build/rondo_headless <game.cue> --frames 3000 --shot-every 150 --out work/shots \
    [--log-bios | --log-bios-all] [--press FRAME:run,i:LEN] [--profile] \
    [--watch BANK:ADDR] [--trace FRAME:COUNT] [--bios-reset syscard3.pce]
```

Investigation options: `--script` (the capture input script), `--ram 30,94,98` (print those
work-RAM bytes whenever one changes), `--poke ADDR=VAL` (write before every frame),
`--save-state FRAME:FILE` / `--load-state FILE` (start later runs mid-game), `--dump-ram
FRAME:FILE`, `--ram-trace FILE` (work RAM + pad after every frame, for offline searches),
`--cheats health,hearts,lives,maria,play-maria` and `--skip-cinematics` (the frontend's
`game.c`), `--run FRAME:LEN` (hold the run button), `--wide N` (widescreen, N pixels a side),
`--wide-report FRAME` (which widescreen patches are in), `--write-watch ADDR,...` (which
instruction changes those work-RAM bytes; interpreter), `--vram-watch LO:HI` (which
instructions write that VRAM range), `--dump-mem FRAME:FILE` (all 256 banks + MPRs, for
disassembly), `--dump-vram FRAME:FILE`, `--dump-sat FRAME` (sprite table), `--scroll-log FILE`
(scroll registers mid-screen per frame). Button
names for `--press` are lower case: `i`, `ii`, `select`, `run`, `up`, `down`, `left`, `right`.

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
