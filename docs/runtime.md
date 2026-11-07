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
