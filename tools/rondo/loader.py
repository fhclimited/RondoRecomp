"""Rondo of Blood's table-driven overlay loader, reimplemented for offline analysis.

Two variants exist, reverse engineered from the game:

Boot loader (boot sector, $309C; table at $312F, only group 0 is valid):
  group entry: 5 bytes [lba lo, mid, hi, cmdlist lo, hi]; lba is absolute,
  the loader subtracts the track 2 start ($0F32 = 3890) to get a record number.
  commands:
      $80-$FF            end
      $00 b4 b5 b6       MPR4/5/6 = ram_base + b4/b5/b6
      nn  off cnt        read cnt sectors from record (group + off) to $8000

Main loader (bank $6A, $6FD8; group table at $74D1) extends the commands:
      $C0 lo hi          continue with the command list at hi:lo
      $80 $82 $83        post-process via a routine at $8006 in bank ram_base+4
      $81                copy $C000-$D3FF -> $A000 with MPR5 = ram_base+4
      other $8x-$FF      end
      nn  off cnt        read to the address in a destination table indexed by nn
"""
from __future__ import annotations

from dataclasses import dataclass, field

from pce.analysis import Memory

TRACK2_LBA = 0x0F32
RAM_BASE = 0x68  # first Super System Card RAM bank (EX_MEMOPEN)


@dataclass
class LoadCmd:
    kind: str                  # "map" | "read" | "post" | "goto"
    banks: tuple[int, ...] = ()
    record: int = 0
    count: int = 0
    dest: int = 0x8000
    op: int = 0


@dataclass
class Group:
    index: int
    lba: int
    cmdlist: int
    cmds: list[LoadCmd] = field(default_factory=list)
    error: str | None = None

    @property
    def record(self) -> int:
        return self.lba - TRACK2_LBA

    @property
    def sectors(self) -> int:
        return sum(c.count for c in self.cmds if c.kind == "read")


def parse_group(read, index: int, table: int, ram_base: int = RAM_BASE,
                extended: bool = False, dest_table: dict[int, int] | None = None,
                max_cmds: int = 128) -> Group | None:
    """Parse group `index` from the table at logical `table`, reading via `read(addr)`."""
    e = table + 5 * index
    raw = [read(e + i) for i in range(5)]
    if None in raw:
        return None
    lba = raw[0] | raw[1] << 8 | raw[2] << 16
    ptr = raw[3] | raw[4] << 8
    g = Group(index, lba, ptr)
    p = ptr
    seen = set()
    for _ in range(max_cmds):
        if p in seen:
            g.error = "loop"
            return g
        seen.add(p)
        flag = read(p)
        if flag is None:
            g.error = f"unmapped ${p:04X}"
            return g
        if flag & 0x80:
            if not extended:
                return g
            if flag == 0xC0:
                lo, hi = read(p + 1), read(p + 2)
                if None in (lo, hi):
                    g.error = "unmapped goto"
                    return g
                g.cmds.append(LoadCmd("goto", dest=lo | hi << 8))
                p = lo | hi << 8
                continue
            if flag in (0x80, 0x81, 0x82, 0x83):
                g.cmds.append(LoadCmd("post", op=flag))
                p += 1
                continue
            return g
        if flag == 0:
            b = [read(p + i) for i in (1, 2, 3)]
            if None in b:
                g.error = "unmapped map"
                return g
            g.cmds.append(LoadCmd("map", banks=tuple(ram_base + x for x in b)))
            p += 4
        else:
            off, cnt = read(p + 1), read(p + 2)
            if None in (off, cnt):
                g.error = "unmapped read"
                return g
            dest = 0x8000
            if extended:
                if dest_table is None or flag not in dest_table:
                    g.error = f"read flag {flag:#x} without destination"
                    return g
                dest = dest_table[flag]
            g.cmds.append(LoadCmd("read", record=g.record + off, count=cnt, dest=dest, op=flag))
            p += 3
    g.error = "no terminator"
    return g


def apply_group(g: Group, mem: Memory, disc, track) -> None:
    """Perform the group's map/read commands against `mem` (MPR4-6 updated in place).
    Post-process commands ($80-$83) are not emulated; their effect is listed only."""
    for c in g.cmds:
        if c.kind == "map":
            mem.mpr[4:7] = list(c.banks)
        elif c.kind == "read":
            mem.load(c.dest, disc.read_records(track, c.record, c.count))
