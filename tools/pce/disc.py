"""Read-only access to PC Engine CD-ROM² disc images described by a CUE sheet.

Supports the common dump layouts: one file per track (ISO 2048 / WAV / BIN 2352)
or a single multi-track BIN. Data is addressed the way the System Card does it:
a "record" number relative to the start (INDEX 01) of a data track.
"""
from __future__ import annotations

import os
import re
import shlex
import struct
from dataclasses import dataclass, field

FRAMES_PER_SECOND = 75


def msf_to_frames(msf: str) -> int:
    m, s, f = (int(x) for x in msf.split(":"))
    return (m * 60 + s) * FRAMES_PER_SECOND + f


def frames_to_msf(frames: int) -> str:
    m, rem = divmod(frames, 60 * FRAMES_PER_SECOND)
    s, f = divmod(rem, FRAMES_PER_SECOND)
    return f"{m:02d}:{s:02d}:{f:02d}"


SECTOR_SIZES = {
    "AUDIO": 2352,
    "MODE1/2048": 2048,
    "MODE1/2352": 2352,
}


@dataclass
class Track:
    number: int
    mode: str
    path: str
    data_offset: int          # byte offset of the file's first sector (skips WAV header)
    sector_size: int
    file_index: int = 0       # INDEX 01 position inside the file, in sectors
    index00: int | None = None
    pregap: int = 0           # PREGAP frames (not stored in the file)
    sectors: int = 0          # length in sectors, from INDEX 01
    lba: int = 0              # absolute LBA of INDEX 01

    @property
    def is_data(self) -> bool:
        return self.mode != "AUDIO"

    @property
    def user_offset(self) -> int:
        # MODE1/2352 sectors carry 12 sync + 4 header bytes before user data
        return 16 if self.mode == "MODE1/2352" else 0


def _wav_data_offset(path: str) -> tuple[int, int]:
    """Return (offset, length) of the PCM 'data' chunk of a RIFF WAV file."""
    with open(path, "rb") as f:
        head = f.read(12)
        if head[:4] != b"RIFF" or head[8:12] != b"WAVE":
            raise ValueError(f"{path}: not a RIFF/WAVE file")
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                raise ValueError(f"{path}: no data chunk")
            cid, size = hdr[:4], struct.unpack("<I", hdr[4:])[0]
            if cid == b"data":
                return f.tell(), size
            f.seek(size + (size & 1), os.SEEK_CUR)


class Disc:
    def __init__(self, cue_path: str):
        self.cue_path = os.path.abspath(cue_path)
        self.tracks: list[Track] = []
        self._parse()

    def _parse(self) -> None:
        base = os.path.dirname(self.cue_path)
        files: list[tuple[str, str, list[Track]]] = []
        cur: Track | None = None
        with open(self.cue_path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = raw.strip()
                if not line:
                    continue
                parts = shlex.split(line, posix=True)
                cmd = parts[0].upper()
                if cmd == "FILE":
                    files.append((os.path.join(base, parts[1]), parts[2].upper(), []))
                elif cmd == "TRACK":
                    mode = parts[2].upper()
                    if mode not in SECTOR_SIZES:
                        raise ValueError(f"unsupported track mode {mode}")
                    path, ftype, tlist = files[-1]
                    cur = Track(int(parts[1]), mode, path, 0, SECTOR_SIZES[mode])
                    tlist.append(cur)
                elif cmd == "PREGAP" and cur:
                    cur.pregap = msf_to_frames(parts[1])
                elif cmd == "INDEX" and cur:
                    pos = msf_to_frames(parts[2])
                    if int(parts[1]) == 0:
                        cur.index00 = pos
                    elif int(parts[1]) == 1:
                        cur.file_index = pos

        cursor = 0  # absolute LBA at which the current file starts
        for path, ftype, tlist in files:
            if ftype == "WAVE":
                off, size = _wav_data_offset(path)
            else:
                off, size = 0, os.path.getsize(path)
            gaps = 0
            for i, t in enumerate(tlist):
                t.data_offset = off
                file_sectors = size // t.sector_size
                gaps += t.pregap
                t.lba = cursor + gaps + t.file_index
                end = file_sectors
                if i + 1 < len(tlist):
                    nxt = tlist[i + 1]
                    end = nxt.index00 if nxt.index00 is not None else nxt.file_index
                t.sectors = end - t.file_index
                self.tracks.append(t)
            cursor += gaps + size // tlist[0].sector_size

    def track(self, number: int) -> Track:
        for t in self.tracks:
            if t.number == number:
                return t
        raise KeyError(number)

    @property
    def data_tracks(self) -> list[Track]:
        return [t for t in self.tracks if t.is_data]

    def read_records(self, track: Track, record: int, count: int = 1) -> bytes:
        """Read `count` 2048-byte user-data sectors starting at `record` (track-relative)."""
        if not track.is_data:
            raise ValueError("not a data track")
        if record < 0 or record + count > track.sectors:
            raise ValueError(f"record range {record}+{count} outside track ({track.sectors})")
        out = bytearray()
        with open(track.path, "rb") as f:
            for r in range(record, record + count):
                pos = track.data_offset + (track.file_index + r) * track.sector_size + track.user_offset
                f.seek(pos)
                out += f.read(2048)
        return bytes(out)

    def read_track(self, track: Track) -> bytes:
        return self.read_records(track, 0, track.sectors)


@dataclass
class IplHeader:
    """System Card IPL information block (record 1 of the first data track)."""
    iplblk: int       # record number of the boot program
    iplbln: int       # boot program length in records
    iplsta: int       # logical load address
    ipljmp: int       # logical entry point
    mpr: list[int]    # MPR2..MPR6 bank numbers to map while loading/running
    openmode: int
    grpblk: int
    grpbln: int
    grpadr: int
    adpblk: int
    adpbln: int
    adprate: int
    system_id: str
    copyright: str
    title: bytes
    raw: bytes = field(repr=False, default=b"")

    @classmethod
    def parse(cls, sector: bytes) -> "IplHeader":
        b = sector
        rec = lambda o: (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]
        return cls(
            iplblk=rec(0x00), iplbln=b[0x03],
            iplsta=b[0x04] | (b[0x05] << 8), ipljmp=b[0x06] | (b[0x07] << 8),
            mpr=list(b[0x08:0x0D]), openmode=b[0x0D],
            grpblk=rec(0x0E), grpbln=b[0x11], grpadr=b[0x12] | (b[0x13] << 8),
            adpblk=rec(0x14), adpbln=b[0x17], adprate=b[0x18],
            system_id=b[0x20:0x37].decode("ascii", "replace"),
            copyright=b[0x38:0x6A].split(b"\0")[0].decode("ascii", "replace"),
            title=b[0x6A:0x80], raw=b[:0x80],
        )
