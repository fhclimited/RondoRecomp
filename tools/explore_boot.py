"""Load the IPL boot program the way the System Card does, then map its code.

usage: python tools/explore_boot.py <game.cue> [--out work/]
Writes a listing to <out>/boot.asm and prints BIOS calls (with resolved
arguments), bank switches, block copies and unresolved targets.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pce.analysis import Explorer, Memory  # noqa: E402
from pce.disc import Disc, IplHeader  # noqa: E402

# IPL MPR values are taken as offsets into CD RAM ($80+n): an assumption to confirm by tracing.
CD_RAM_BASE = 0x80


def fmt(v):
    return "??" if v is None else f"${v:02X}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("cue")
    ap.add_argument("--out", default="work")
    args = ap.parse_args()

    disc = Disc(args.cue)
    track = disc.data_tracks[0]
    ipl = IplHeader.parse(disc.read_records(track, 1))
    mpr = [0xFF, 0xF8] + [CD_RAM_BASE + m for m in ipl.mpr] + [0x00]
    mem = Memory(mpr)
    blob = disc.read_records(track, ipl.iplblk, ipl.iplbln)
    mem.load(ipl.iplsta, blob)

    ex = Explorer(mem)
    ex.add_entry(ipl.ipljmp, note="IPL entry")
    ex.run()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "boot.bin").write_bytes(blob)
    (out / "boot.asm").write_text(ex.listing(), encoding="utf-8")

    code = sum(i.length for i in ex.instrs.values())
    print(f"Boot program: {len(blob)} bytes at ${ipl.iplsta:04X}, entry ${ipl.ipljmp:04X}")
    print(f"  {len(ex.instrs)} instructions ({code} bytes of code), {len(ex.labels)} labels")
    print("Events:")
    for e in sorted(ex.events, key=lambda e: e.site):
        d = e.detail
        if e.kind == "bios" and not d.get("tail"):
            regs = " ".join(f"{k}={fmt(d[k])}" for k in ("_al", "_bl", "_bh", "_cl", "_ch", "_dl", "_dh"))
            print(f"  ${e.site:04X}  {d['name']:<10s} A={fmt(d['A'])} X={fmt(d['X'])}  {regs}")
        elif e.kind == "tam":
            print(f"  ${e.site:04X}  TAM mask={fmt(d['mask'])} bank={fmt(d['value'])}")
        elif e.kind == "unresolved":
            print(f"  ${e.site:04X}  leaves loaded memory (MPR{d['window']}={fmt(d['bank'])})")
    print(f"Listing written to {out / 'boot.asm'}")


if __name__ == "__main__":
    main()
