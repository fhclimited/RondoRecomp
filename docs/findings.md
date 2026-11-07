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

## Open questions

- [ ] Role of track 22 / second `CD_BASE` (error-retry mirror?) and which track carries the translation
- [ ] Post-process routine at `$DB+4:$8006` (decompressor?) — ops `$80/$82/$83`
- [ ] Validate heuristic jump tables and resolve RAM-vector dispatch (needs traces)
- [ ] Code inside stage/scene overlays: entry points and size per overlay
- [x] Confirm System Card entry names against a 3.0 dump (done; 4 trailing entries unnamed, unused)
