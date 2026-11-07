"""Enumerate every overlay group in the main loader's table.

usage: python tools/list_overlays.py <game.cue> [--verbose]
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pce.analysis import Memory  # noqa: E402
from pce.disc import Disc, IplHeader  # noqa: E402
from rondo.loader import RAM_BASE, apply_group, parse_group  # noqa: E402

BOOT_TABLE = 0x312F
MAIN_TABLE = 0x74D1
DEST_TABLE = 0x70F8      # word table indexed by read flag (flag*2)
ADPCM_TABLE = 0x7135     # 4-byte entries: lba24, sector count


def boot_image(disc, track) -> Memory:
    ipl = IplHeader.parse(disc.read_records(track, 1))
    mem = Memory([0xFF, 0xF8, 0x80, 0x81, 0x82, 0x83, 0x84, 0x00])
    mem.load(ipl.iplsta, disc.read_records(track, ipl.iplblk, ipl.iplbln))
    apply_group(parse_group(mem.read, 0, BOOT_TABLE), mem, disc, track)
    mem.mpr[2], mem.mpr[3] = RAM_BASE + 1, RAM_BASE + 2
    return mem


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("cue")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    disc = Disc(args.cue)
    track = disc.data_tracks[0]
    mem = boot_image(disc, track)
    rd = mem.read
    dest = {n: rd(DEST_TABLE + 2 * n) | rd(DEST_TABLE + 2 * n + 1) << 8 for n in (1, 2, 3)}
    print("read destinations: " + ", ".join(f"{n}->${a:04X}" for n, a in dest.items()))

    groups = []
    for i in range(256):
        g = parse_group(rd, i, MAIN_TABLE, extended=True, dest_table=dest)
        if g is None or g.error or not (0 <= g.record < track.sectors):
            reason = "unreadable" if g is None else (g.error or f"record {g.record} out of range")
            print(f"table ends at index {i} ({reason})")
            break
        groups.append(g)

    covered = set()
    bank_use = Counter()
    print(f"\n{len(groups)} overlay groups")
    print(" idx  record  sectors  banks loaded                         post-ops")
    for g in groups:
        banks = []
        mpr = [None, None, None]
        for c in g.cmds:
            if c.kind == "map":
                mpr = list(c.banks)
            elif c.kind == "read":
                covered.update(range(c.record, c.record + c.count))
                first = (c.dest - 0x8000) >> 13
                nb = (c.count * 2048 + (c.dest & 0x1FFF) + 0x1FFF) >> 13
                for k in range(first, min(3, first + nb)):
                    if mpr[k] is not None and mpr[k] not in banks:
                        banks.append(mpr[k])
        for b in banks:
            bank_use[b] += 1
        posts = " ".join(f"${c.op:02X}" for c in g.cmds if c.kind == "post")
        print(f" {g.index:3d}  {g.record:6d}  {g.sectors:7d}  {' '.join(f'{b:02X}' for b in banks):36s} {posts}")
        if args.verbose:
            for c in g.cmds:
                if c.kind == "map":
                    print(f"        map {' '.join(f'{b:02X}' for b in c.banks)}")
                elif c.kind == "read":
                    print(f"        read {c.record} x {c.count} -> ${c.dest:04X}")
                else:
                    print(f"        {c.kind} {c.op:#04x} {c.dest:#06x}")

    print(f"\nSectors referenced by groups: {len(covered)} of {track.sectors} in track {track.number}")
    print("Super RAM banks by number of groups writing them:")
    print("  " + " ".join(f"{b:02X}:{n}" for b, n in sorted(bank_use.items())))

    # ADPCM table
    ad = []
    for i in range(128):
        e = ADPCM_TABLE + 4 * i
        raw = [rd(e + k) for k in range(4)]
        if None in raw:
            break
        lba = raw[0] | raw[1] << 8 | raw[2] << 16
        rec = lba - 0x0F32
        if not (0 <= rec < track.sectors) or raw[3] == 0:
            break
        ad.append((rec, raw[3]))
    print(f"\nADPCM table: {len(ad)} clips, {sum(n for _, n in ad)} sectors")


if __name__ == "__main__":
    main()
