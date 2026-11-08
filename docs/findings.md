# Rondo of Blood: reverse-engineering notes

Target image: "Castlevania Rondo of Blood [English]" (CUE + 2 Mode1/2048 ISO + 20 WAV).
Everything below was derived with the tools in `tools/`; addresses are logical
(CPU) addresses unless written `bank:addr`.

## Disc layout

| Track | Type | LBA | Records | Notes |
|---|---|---|---|---|
| 1 | audio | 0 | 3665 | warning track |
| 2 | data | 3890 (`$0F32`) | 10149 | **the track the game reads** |
| 3-21 | audio | 14189… | | CD-DA music |
| 22 | data | 211262 | 13300 | second base (via `CD_BASE`); same layout as track 2 for most records, differs in ~1,650 records; tail is zero padding |

Record numbers are relative to track 2's INDEX 01. Loader tables store absolute
LBAs and subtract `$0F32`, which confirms the 3:00 pregap layout.

## Boot sequence

1. IPL header (track 2, record 1): load 1 record from record 2 to `$3000` (work RAM), jump `$3000`.
   MPR2-6 header bytes are `$00-$04` (assumed relative to `$80`; unverified, and unused since the
   boot code lives in work RAM).
2. Boot program (`$3000`):
   - clears VRAM through the VDC ports
   - `CD_BASE` ×2: base 1 = track `$02`, base 2 = track `$22` (BCD)
   - `EX_GETVER`: System Card < 3 → loads 20 records from record 35 to `$4000` and runs it
     (presumably the "Super System Card required" screen)
   - `EX_MEMOPEN` (`$E0DE`) → A = first Super RAM bank (`$68`), stored at zp `$DB`
   - runs boot loader group 0, maps MPR2/3 = `$69`/`$6A`, jumps `$4000`
3. Main init (`$40F9`): clears work RAM (overwrites the boot sector), sets screen mode,
   loads main-table groups 4 and 6 and an ADPCM clip, installs IRQ vectors with `EX_SETVEC`
   (1 → `$4177`, 2 → `$4297`), then idles in `JMP *`. **All game logic runs from interrupts.**

## Overlay loader format

Group table: 5-byte entries `[lba lo, lba mid, lba hi, cmdlist lo, cmdlist hi]`.

Command list:

| Bytes | Meaning |
|---|---|
| `00 b4 b5 b6` | MPR4/5/6 = `$DB` + b4/b5/b6 |
| `nn off cnt` (nn `$01-$7F`) | CD_READ `cnt` records from group record + `off` to dest[nn] |
| `C0 lo hi` | continue at command list `hi:lo` (main loader only) |
| `80`, `82`, `83` | post-process via routine at `$8006` in bank `$DB+4` (main loader only) |
| `81` | copy `$C000-$D3FF` → `$A000` with MPR5 = `$DB+4` (main loader only) |
| other `$8x-$FF` | end |

- Boot loader: `$309C`, table `$312F` (only group 0 valid), dest always `$8000`.
- Main loader: `6A:6FD8`, table `$74D1`, dest table `$70F8` (1→`$8000`, 2→`$9000`, 3→`$9400`).
- ADPCM loader: `6A:6E43`, table `$7135` (4-byte entries: lba24, record count), uses `AD_TRANS`.

## Overlay inventory (`tools/list_overlays.py`)

- 95 main-table groups:
  - 24 stage loads (groups 16-87): two 8-record loads into `$7A/$7B` plus one 73-89 record
    load across ~16 banks (post-ops `$80 $81`)
  - 9 × 96-record loads into `$78-$87` (post-op `$82`): scenes / cutscenes, to be confirmed
  - engine/menu groups 0-15, 88-94
- Banks `$69 $6A $75 $76 $77` are loaded once at boot and never reloaded (40 KiB resident).
- Groups cover 3,672 records; ADPCM table 55 clips / 1,791 records; ~4,500 records remain
  (expected: graphics streamed by overlay code directly).

## Resident code (`tools/explore_main.py`)

Bank-aware recursive descent from `$4000` plus both IRQ vectors, after groups 0/4/6:

- ~6,600 instructions / ~14.4 KB of code in banks `$69 $6A $6B $6C $72 $75 $76`
- 96 TAM sites (35 with runtime-computed banks), 12 indirect jumps
  (10 tables found heuristically, 2 through RAM vectors at `$2000`: dynamic dispatch)
- BIOS surface: 24 entries (CD read/seek/play/pause/stat/fade/search, ADPCM
  trans/play/cplay/stop/stat, BM_* saves, EX_* screen/vector/joypad/font, MA_MUL8U)

## System Card 3.0 (checked against a Japanese v3.0 dump, `tools/explore_bios.py`)

- The jump table at `$E000` has 81 entries. Our 77 names line up, including `EX_MEMOPEN` at `$E0DE`.
- Interrupt dispatch: each hardware vector handler tests one bit of `irq_m` (zp `$F5`).
  If the bit is set, it jumps straight through a work-RAM vector and does nothing else:
  bit 0 → IRQ2 `($2200)`, bit 1 → IRQ1 `($2202)`, bit 2 → timer `($2204)`, bit 3 → NMI `($2206)`.
  When the bit is clear, the BIOS IRQ1 handler does its own frame work: reads VDC status,
  updates the scroll registers from RAM, keeps a frame counter, runs the PSG driver, reads
  the joypads, and optionally calls the vsync (`$2208`, bit 4) and hsync (`$220A`, bit 6) hooks.
- **Rondo sets `irq_m = $06`.** Its IRQ1 (`$4177`) and timer (`$4297`) handlers therefore get the
  hardware interrupts directly, and the BIOS does no per-frame or per-tick work for this game.
  Only IRQ2 (CD/ADPCM) stays with the BIOS, and it is only used inside BIOS CD/ADPCM calls.
- Runtime consequence: the BIOS is needed only as a library of 24 functions. Interrupts can be
  sent straight to the game's handlers without emulating BIOS interrupt code.

## Frame structure (observed at runtime)

- The vblank handler (`$4177`) saves registers and MPR4-6 and reads VDC status. If the raster
  flag is set, it runs the raster dispatcher (table `$44E9`); if the vblank flag is not set, it
  exits. Otherwise it counts the frame (`$47`), re-enables interrupts and runs the frame logic,
  guarded against re-entry by a busy flag (`$33`).
- The second dispatcher (`69:44E6`) selects per-scene raster effects. Entry 0 means "none".
- System Card calls seen from boot to the save menu: CD_READ, CD_PLAY, CD_FADE, CD_STAT,
  AD_TRANS, AD_PLAY, AD_STOP, AD_STAT (every frame), EX_JOYSNS (every frame), EX_GETFNT,
  BM_READ/WRITE/DELETE/FREE (save setup), EX_SCRSIZ.
- EX_GETFNT: the Japanese disc's attract-mode story (frames ~577-6276 of the capture script,
  252 glyphs, 12x12) is drawn from the System Card's kanji font; the English translation uses
  its own font except for "ＤＲＡＣＵＬＡ" (full-width, ~frame 5976).

## Disc releases

| Release | Layout | Data track 2 | Data track 22 |
|---|---|---|---|
| Akumajou Dracula X: Chi no Rondo (JP), e.g. a CloneCD .img/.cue | 22 tracks | original | original |
| English translation (BIN/ISO/WAV per track, 2012 cue) | 22 tracks | translated (1660 sectors differ) | original (+3300 sectors) |
| English translation (redump-style BIN per track, 2024) | 22 tracks | the same as above | translated too |

All share the track layout (CD audio is found by absolute position, so it must match) and the
boot sector (fingerprint `DB4D9415EC501573`); the code differs little (an English-built
`rondo.exe` runs the Japanese disc 99% native). Game RAM, the widescreen patch sites, run and
Maria behave the same on both.

## Game state (work RAM, found by watching the game; `tools/frontend/game.h`)

Addresses are offsets in work RAM (bank $F8; $2000+ in the game's address space).

| Offset | Meaning |
|---|---|
| $30 | main state: 00 boot, 02 opening, 03 title, 04 save menu, 05 playing, 06 area change, 08 reset, 0A story cinematic |
| $31 | sub-state of $30 |
| $8D | lives, BCD (3 at the start) |
| $94 | hearts, BCD (shown on the HUD; 10 at the start) |
| $98 | health; $5C (92) is a full bar |
| $A2 | current area (00 before Stage 0, 0A, 10, ...) |

- RUN skips the opening (state 02) but not the Stage 0 cinematic (state 0A, frames 12531-16098
  of the capture run).
- Undoing health losses after each frame keeps Richter alive (verified through the hit at
  frame 17865 and the death at 21924 of the capture run); undoing the lives loss at that death
  leaves the game restarting the area normally.
- $1460 is not an invincibility switch in this version: writing 01 every frame made Richter
  take more hits (it is likely a per-object timer).
- Input: presses shorter than 3 frames are ignored; a second I press 2-8 frames after the
  first one ends makes Richter backflip.

## Characters, save record, player movement

| Where | Meaning |
|---|---|
| zp $9F | character: 0 Richter, 1 Maria. Each stage loads that character's code; the area number has it in bit 0 (Stage 0 is $0A / $0B) |
| $1F40-$1F56 | the current game's save record (copied to backup RAM on saving): $1F40 bit 0 lets the player change to Maria (not offered in the Prologue); $1F41-$1F42 a BCD counter; $1F43-$1F4C areas visited, $1F4D-$1F56 areas cleared (bit per area, one half per character) |
| $0D80 / $0DD8 | player (object slot 0) X velocity, high / low, 8.8 pixels a frame |
| $0AC0 | player facing (0 right, 1 left) |
| $0F90 | player animation frame timer (counts down to the next frame) |

Richter walks at $0140 (1.25 px a frame), jumps at $0150, Maria walks and jumps at $0160. The
walking speed is set once as a walk starts (a two-entry table by facing in Richter's code).
Forcing $9F = 1 before Stage 0 loads plays the Prologue as Maria.

## Camera, scrolling and objects (widescreen)

- Scroll layers at $1E00+ (8 layers): X sub/low/high $1E00/$1E08/$1E10, Y $1E18/$1E20/$1E28,
  velocities $1E6E-$1E86, map page counters $1E8E/$1E96, per-frame copies at $1E39+ for the
  raster split. Layer 0 is the playfield camera; the camera's limits are $1EBE (left page) and
  $1EC6/$1EC7 (right X). The camera follows the player to keep it at screen X 128.
- The level is a grid of 256-pixel screens ($1D00 table, 16 x 16 pages); the background map
  is 512 x 512 (MWR $50). The scroll code streams 32-pixel columns 64 px past the edge it moves
  towards, reading that distance from a small table also used for vertical streaming.
- Objects: 88 slots in parallel arrays at $2A10-$3A38 (+$2000 in CPU space); positions are in
  screen coordinates and are shifted by the camera every frame. Objects spawn from a per-room
  list sorted by world X when within 64 px of the screen, are removed beyond 128 px, and (for
  some) only move within 64 px. Slot $57 is the HUD. Some enemy generators sit fixed to the
  screen at X 320 (Stage 1's skeletons).
- The sprite builder writes up to 64 sprites straight to the VDC and drops any piece whose X is
  outside the 256-pixel screen.
- The Stage 0 carriage chase does not scroll the camera; its scenery layers loop round the
  whole 512-pixel map, part of which is shared between layers. Its raster bands (rows of the
  picture, X scroll): sky 0-62 (+1 every few frames), far trees 63-78 (+1), trees 79-126
  (+4), carriage 127-182, grass 183-224 (+8). For the first ~400 frames the carriage
  (background) is parked in the band's map at scroll 256, just off both edges of the 256
  visible pixels, and its horses (sprite objects 3B, 4B-4E, 51-53 at screen X -224..-72,
  static) just off the left, while the trees (sprite objects, 16x64 pieces) go by; then the
  band scrolls and the carriage drives in from the left.
- The sprite builder (72:8000) keeps the object slot it is drawing in zp `$D9`; pieces with
  X + 32 negative are dropped at 72:8127, those with X + 32 below a size-dependent bound
  ($81A1,X) at 72:813A.

## Rooms (remaster mode)

Each stage's overlay keeps its rooms in tables (Stage 1, bank $6B; read by the room loader at
6B:8889 and the entry loader at 6B:8952):

- zp `$82` is the room, `$83` the entry (where the player comes in). A room's grid of screens
  (`$1D00`, 16 x 16 pages of 256 x 256 pixels, a screen number each, 0 = none) is unpacked
  from `$9C30 + 2 * room`: a start index, then screen numbers; one with bit 7 set ends a run
  and the next byte is a new start index; `$FF` ends.
- An entry (`$9C71 + 2 * entry`, 14 bytes): camera X page, Y page, X low, Y low, `$3EB6`,
  `$3E66`, the camera's left, right, top and bottom pages (`$1EBE`, `$1EC6`, `$1ECE`,
  `$1ED6`), the player's screen X and Y (X 0: keep the camera), the room-start drawing
  variant (`$45`), the player's facing.
- Stage 1 has three rooms: the street (camera 0-1024), the staircase house (its camera
  unlocks as you climb) and the town beyond. Moving between them is the stage's own code:
  the street's handler starts the next room (`INC $82`, entry from `$9C20 + room`) when the
  player reaches screen X 240 at the camera's right limit; doors set the entry from `$8C`.
  No CD access: the next room's tiles are unpacked from RAM (bank $6C code).
- A grid can hold screens the camera never shows from that entry (the street's grid goes on
  into a later boss arena, room 3 uses the same grid); what the camera can reach is its
  limits.
- Drawing (bank $6F, the column streamer at 6F:A7A5): a screen number gives 64 block
  numbers (8 x 8 blocks of 32 pixels) at `S + screen * 64` and 8 bytes of their ninth bits
  (a bit per column) at `B + screen * 8`; a block is 4 x 4 background map words (32 bytes) at
  `C + block * 32` in bank `$DB + K + ninth bit`. For Stage 1, S = $A9EC, B = $AFEC (both in
  bank $6F), C = $C000, K = $0B. All of a room's tiles are in VRAM while it is played.
- The camera: the logic runs from vblank into the next frame's first lines, and moves the
  camera on the way (layer 0's Y is set 64 px up at 6F:A6A1 and clamped back at 6F:A384, two
  lines later). The frame is drawn from a copy taken as the logic starts (69:41FD): `$1E39`
  X low, `$1E42` X page bit 0, `$1E54` Y low, `$1E5D` Y page bit 0.
- The player (object slot 0): screen X `$0BC8` / `$0B70`, Y `$0CD0` / `$0C78`.

## Open questions

- [ ] Role of track 22 / second `CD_BASE` (error-retry mirror?) and which track carries the translation
- [ ] Post-process routine at `$DB+4:$8006` (decompressor?) — ops `$80/$82/$83`
- [ ] Validate heuristic jump tables and resolve RAM-vector dispatch (needs traces)
- [ ] Code inside stage/scene overlays: entry points and size per overlay
- [x] Confirm System Card entry names against a 3.0 dump (done; 4 trailing entries unnamed, unused)
