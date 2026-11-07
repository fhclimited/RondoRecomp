"""Rebuild the post-init memory image and map the resident program's code.

Boot sequence reproduced: IPL -> boot group 0 -> MPR2/3 = $69/$6A -> main init
loads main-table groups 4 and 6. Code is then discovered from $4000 and from
every interrupt vector the program installs.

usage: python tools/explore_main.py <game.cue> [--out work/]
"""
import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pce.analysis import Explorer  # noqa: E402
from pce.disc import Disc  # noqa: E402
from list_overlays import DEST_TABLE, MAIN_TABLE, boot_image  # noqa: E402
from rondo.loader import RAM_BASE, apply_group, parse_group  # noqa: E402

INIT_GROUPS = (4, 6)


def fmt(v):
    return "??" if v is None else f"${v:02X}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("cue")
    ap.add_argument("--out", default="work")
    args = ap.parse_args()

    disc = Disc(args.cue)
    track = disc.data_tracks[0]
    mem = boot_image(disc, track)
    boot_mpr = list(mem.mpr)
    rd = mem.read
    dest = {n: rd(DEST_TABLE + 2 * n) | rd(DEST_TABLE + 2 * n + 1) << 8 for n in (1, 2, 3)}
    for gi in INIT_GROUPS:
        apply_group(parse_group(rd, gi, MAIN_TABLE, extended=True, dest_table=dest), mem, disc, track)
    mem.mpr = boot_mpr  # code at $4000 starts with the boot mapping; TAMs are tracked from there

    ex = Explorer(mem, globals_zp={0xDB: RAM_BASE})
    ex.add_entry(0x4000, note="main entry")
    ex.run()

    out = Path(args.out)
    (out / "main.asm").write_text(ex.listing(), encoding="utf-8")

    by_bank = Counter(b for b, _ in ex.instrs)
    size_by_bank = defaultdict(int)
    for (b, _), ins in ex.instrs.items():
        size_by_bank[b] += ins.length
    total = sum(size_by_bank.values())
    print(f"Resident code reached: {len(ex.instrs)} instructions, {total} bytes, {len(ex.labels)} labels")
    print("  by bank: " + ", ".join(f"${b:02X}:{size_by_bank[b]}B" for b in sorted(size_by_bank)))

    kinds = Counter(e.kind for e in ex.events)
    print("  events: " + ", ".join(f"{k} {n}" for k, n in sorted(kinds.items())))

    for e in ex.events:
        if e.kind == "setvec":
            print(f"  IRQ vector {e.detail['vector']} -> ${e.detail['addr']:04X} (set at ${e.site:04X})")

    tams = [e for e in ex.events if e.kind == "tam"]
    unk = [e for e in tams if e.detail["value"] is None]
    print(f"\nTAM sites: {len(tams)}, value unknown at {len(unk)}")

    ind = [e for e in ex.events if e.kind == "indirect"]
    print(f"Indirect jumps: {len(ind)} ({sum(1 for e in ind if e.detail['exact'])} exact, "
          f"{sum(len(e.detail['targets']) for e in ind)} targets found)")
    for e in ind:
        d = e.detail
        print(f"  {e.bank:02X}:{e.site:04X}  table ${d['table']:04X}  {'exact' if d['exact'] else 'heuristic'}  {len(d['targets'])} targets")

    unres = Counter((e.detail["window"], e.detail["bank"]) for e in ex.events if e.kind == "unresolved")
    print("\nWalks stopped by unmapped/unknown banks (window, bank): " +
          ", ".join(f"MPR{w}={fmt(b)} x{n}" for (w, b), n in sorted(unres.items(), key=lambda x: (x[0][0], x[0][1] or -1))))
    undef = [e for e in ex.events if e.kind == "undefined"]
    print(f"Undefined opcodes hit: {len(undef)}" + ("  " + ", ".join(f"{e.bank:02X}:{e.site:04X}" for e in undef[:12]) if undef else ""))

    bios = Counter(e.detail["name"] for e in ex.events if e.kind == "bios")
    print("\nBIOS entries used: " + ", ".join(f"{n} x{c}" for n, c in bios.most_common()))
    print(f"\nListing written to {out / 'main.asm'}")


if __name__ == "__main__":
    main()
