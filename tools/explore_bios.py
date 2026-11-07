"""Map a System Card 3.0 BIOS dump: jump table, hardware vectors, interrupt paths.

usage: python tools/explore_bios.py <syscard3.pce> [--out work/]
Writes work/bios.asm (git-ignored) and prints a summary. The BIOS is never
copied into the repository.
"""
import argparse
import hashlib
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pce.analysis import Explorer, Memory  # noqa: E402
from pce.syscard import ENTRY_NAMES  # noqa: E402

KNOWN = {
    "38179df8f4ac870017db21ebcbf53114": "Super CD-ROM System v3.0 (Japan)",
}
VECTORS = {0xFFF6: "IRQ2/BRK", 0xFFF8: "IRQ1 (VDC)", 0xFFFA: "TIMER", 0xFFFC: "NMI", 0xFFFE: "RESET"}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("bios")
    ap.add_argument("--out", default="work")
    args = ap.parse_args()

    rom = Path(args.bios).read_bytes()
    if len(rom) % 0x2000 == 512:
        rom = rom[512:]  # copier header
    md5 = hashlib.md5(rom).hexdigest()
    print(f"BIOS: {len(rom)} bytes, md5 {md5} ({KNOWN.get(md5, 'unrecognised')})")

    mem = Memory([0xFF, 0xF8, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00])
    for b in range(len(rom) // 0x2000):
        mem.banks[b] = bytearray(rom[b * 0x2000:(b + 1) * 0x2000])
    mem.banks[0xF8] = bytearray(0x2000)  # work RAM (contents unknown)
    rd = mem.read

    # jump table: consecutive JMP abs instructions from $E000
    table = []
    a = 0xE000
    while rd(a) == 0x4C:
        table.append((a, rd(a + 1) | rd(a + 2) << 8))
        a += 3
    print(f"Jump table: {len(table)} entries ($E000-${a - 1:04X}); names known for {len(ENTRY_NAMES)}")
    for i, (addr, tgt) in enumerate(table):
        name = ENTRY_NAMES[i] if i < len(ENTRY_NAMES) else "?"
        if i >= len(ENTRY_NAMES) - 6:
            print(f"  ${addr:04X} {name:<12s} -> ${tgt:04X}")

    ex = Explorer(mem)
    for vec, name in VECTORS.items():
        tgt = rd(vec) | rd(vec + 1) << 8
        print(f"Vector {name:<11s} ${vec:04X} -> ${tgt:04X}")
        ex.add_entry(tgt, note=f"vector {name}")
    for i, (addr, tgt) in enumerate(table):
        ex.add_entry(addr, note=ENTRY_NAMES[i] if i < len(ENTRY_NAMES) else f"entry {i}")
    ex.run()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "bios.asm").write_text(ex.listing(), encoding="utf-8")
    code = sum(i.length for i in ex.instrs.values())
    banks = Counter(b for b, _ in ex.instrs)
    print(f"\nReached {len(ex.instrs)} instructions ({code} bytes) in banks "
          + ", ".join(f"${b:02X}" for b in sorted(banks)))
    ram_vectors = sorted({e.detail["table"] for e in ex.events
                          if e.kind == "indirect" and 0x2000 <= e.detail["table"] < 0x4000})
    print("Indirect jumps through work-RAM vectors: " + ", ".join(f"${v:04X}" for v in ram_vectors))
    print(f"Listing written to {out / 'bios.asm'}")


if __name__ == "__main__":
    main()
