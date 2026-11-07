"""Survey a PC Engine CD image: track layout, IPL header, data-track relationship.

usage: python tools/survey_disc.py <game.cue>
"""
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pce.disc import Disc, IplHeader, frames_to_msf  # noqa: E402
from pce.syscard import bank_kind  # noqa: E402


def main(cue: str) -> None:
    disc = Disc(cue)
    print("Tracks:")
    for t in disc.tracks:
        secs = t.sectors / 75
        print(f"  {t.number:2d} {t.mode:11s} LBA {t.lba:6d} ({frames_to_msf(t.lba)})  "
              f"{t.sectors:6d} sectors  {secs:7.1f}s  pregap {t.pregap}")

    data = disc.data_tracks
    first = data[0]
    s0 = disc.read_records(first, 0)
    s1 = disc.read_records(first, 1)
    print(f"\nTrack {first.number} record 0: {'all zero' if not any(s0) else f'{sum(1 for b in s0 if b)} non-zero bytes'}")
    ipl = IplHeader.parse(s1)
    print("IPL header (record 1):")
    print(f"  system id   {ipl.system_id!r}")
    print(f"  copyright   {ipl.copyright!r}")
    print(f"  boot prog   record {ipl.iplblk} x {ipl.iplbln} -> ${ipl.iplsta:04X}, entry ${ipl.ipljmp:04X}")
    print("  MPR2-6      " + " ".join(f"${b:02X}({bank_kind(b)})" for b in ipl.mpr))
    print(f"  openmode    ${ipl.openmode:02X}")
    print(f"  open gfx    record {ipl.grpblk} x {ipl.grpbln} -> VRAM ${ipl.grpadr:04X}")
    print(f"  open adpcm  record {ipl.adpblk} x {ipl.adpbln} rate {ipl.adprate}")

    if len(data) > 1:
        print("\nData track comparison:")
        hashes = {}
        for t in data:
            blob = disc.read_track(t)
            hashes[t.number] = [hashlib.sha1(blob[i:i + 2048]).digest() for i in range(0, len(blob), 2048)]
        a, *rest = data
        for b in rest:
            ha, hb = hashes[a.number], hashes[b.number]
            prefix = 0
            while prefix < min(len(ha), len(hb)) and ha[prefix] == hb[prefix]:
                prefix += 1
            set_b = set(hb)
            shared = sum(1 for h in ha if h in set_b)
            print(f"  track {a.number} vs {b.number}: identical prefix {prefix} records; "
                  f"{shared}/{len(ha)} of track {a.number}'s records occur somewhere in track {b.number}")
            # Where does track b differ: find first mismatch and the extra tail
            if prefix < len(ha):
                print(f"    first difference at record {prefix}")
            if len(hb) > len(ha):
                tail = hb[len(ha):]
                zero = hashlib.sha1(bytes(2048)).digest()
                print(f"    track {b.number} has {len(tail)} extra records "
                      f"({sum(1 for h in tail if h == zero)} all-zero)")

    zero = hashlib.sha1(bytes(2048)).digest()
    blob = disc.read_track(first)
    used = sum(1 for i in range(0, len(blob), 2048) if hashlib.sha1(blob[i:i + 2048]).digest() != zero)
    print(f"\nTrack {first.number}: {used}/{first.sectors} records non-zero")


if __name__ == "__main__":
    main(sys.argv[1])
