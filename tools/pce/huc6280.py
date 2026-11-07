"""HuC6280 instruction set: decoder and disassembler.

The HuC6280 is a 65C02 (with Rockwell bit ops) plus Hudson extensions:
block transfers (TII/TDD/TIN/TIA/TAI), VDC stores (ST0/ST1/ST2), the MMU
(TAM/TMA), register swaps, CLA/CLX/CLY, TST, BSR, SET and CSL/CSH.
The 22 undefined opcodes execute as 1-byte NOPs.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

# mode -> instruction length in bytes
MODE_LEN = {
    "imp": 1, "acc": 1,
    "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "izx": 2, "izy": 2, "izp": 2, "rel": 2,
    "abs": 3, "abx": 3, "aby": 3, "ind": 3, "iax": 3, "zpr": 3, "imzp": 3, "imzpx": 3,
    "imab": 4, "imabx": 4,
    "blk": 7,
}

_TABLE = """
00 BRK imp|01 ORA izx|02 SXY imp|03 ST0 imm|04 TSB zp|05 ORA zp|06 ASL zp|07 RMB0 zp|08 PHP imp|09 ORA imm|0A ASL acc|0C TSB abs|0D ORA abs|0E ASL abs|0F BBR0 zpr
10 BPL rel|11 ORA izy|12 ORA izp|13 ST1 imm|14 TRB zp|15 ORA zpx|16 ASL zpx|17 RMB1 zp|18 CLC imp|19 ORA aby|1A INC acc|1C TRB abs|1D ORA abx|1E ASL abx|1F BBR1 zpr
20 JSR abs|21 AND izx|22 SAX imp|23 ST2 imm|24 BIT zp|25 AND zp|26 ROL zp|27 RMB2 zp|28 PLP imp|29 AND imm|2A ROL acc|2C BIT abs|2D AND abs|2E ROL abs|2F BBR2 zpr
30 BMI rel|31 AND izy|32 AND izp|34 BIT zpx|35 AND zpx|36 ROL zpx|37 RMB3 zp|38 SEC imp|39 AND aby|3A DEC acc|3C BIT abx|3D AND abx|3E ROL abx|3F BBR3 zpr
40 RTI imp|41 EOR izx|42 SAY imp|43 TMA imm|44 BSR rel|45 EOR zp|46 LSR zp|47 RMB4 zp|48 PHA imp|49 EOR imm|4A LSR acc|4C JMP abs|4D EOR abs|4E LSR abs|4F BBR4 zpr
50 BVC rel|51 EOR izy|52 EOR izp|53 TAM imm|54 CSL imp|55 EOR zpx|56 LSR zpx|57 RMB5 zp|58 CLI imp|59 EOR aby|5A PHY imp|5D EOR abx|5E LSR abx|5F BBR5 zpr
60 RTS imp|61 ADC izx|62 CLA imp|64 STZ zp|65 ADC zp|66 ROR zp|67 RMB6 zp|68 PLA imp|69 ADC imm|6A ROR acc|6C JMP ind|6D ADC abs|6E ROR abs|6F BBR6 zpr
70 BVS rel|71 ADC izy|72 ADC izp|73 TII blk|74 STZ zpx|75 ADC zpx|76 ROR zpx|77 RMB7 zp|78 SEI imp|79 ADC aby|7A PLY imp|7C JMP iax|7D ADC abx|7E ROR abx|7F BBR7 zpr
80 BRA rel|81 STA izx|82 CLX imp|83 TST imzp|84 STY zp|85 STA zp|86 STX zp|87 SMB0 zp|88 DEY imp|89 BIT imm|8A TXA imp|8C STY abs|8D STA abs|8E STX abs|8F BBS0 zpr
90 BCC rel|91 STA izy|92 STA izp|93 TST imab|94 STY zpx|95 STA zpx|96 STX zpy|97 SMB1 zp|98 TYA imp|99 STA aby|9A TXS imp|9C STZ abs|9D STA abx|9E STZ abx|9F BBS1 zpr
A0 LDY imm|A1 LDA izx|A2 LDX imm|A3 TST imzpx|A4 LDY zp|A5 LDA zp|A6 LDX zp|A7 SMB2 zp|A8 TAY imp|A9 LDA imm|AA TAX imp|AC LDY abs|AD LDA abs|AE LDX abs|AF BBS2 zpr
B0 BCS rel|B1 LDA izy|B2 LDA izp|B3 TST imabx|B4 LDY zpx|B5 LDA zpx|B6 LDX zpy|B7 SMB3 zp|B8 CLV imp|B9 LDA aby|BA TSX imp|BC LDY abx|BD LDA abx|BE LDX aby|BF BBS3 zpr
C0 CPY imm|C1 CMP izx|C2 CLY imp|C3 TDD blk|C4 CPY zp|C5 CMP zp|C6 DEC zp|C7 SMB4 zp|C8 INY imp|C9 CMP imm|CA DEX imp|CC CPY abs|CD CMP abs|CE DEC abs|CF BBS4 zpr
D0 BNE rel|D1 CMP izy|D2 CMP izp|D3 TIN blk|D4 CSH imp|D5 CMP zpx|D6 DEC zpx|D7 SMB5 zp|D8 CLD imp|D9 CMP aby|DA PHX imp|DD CMP abx|DE DEC abx|DF BBS5 zpr
E0 CPX imm|E1 SBC izx|E3 TIA blk|E4 CPX zp|E5 SBC zp|E6 INC zp|E7 SMB6 zp|E8 INX imp|E9 SBC imm|EA NOP imp|EC CPX abs|ED SBC abs|EE INC abs|EF BBS6 zpr
F0 BEQ rel|F1 SBC izy|F2 SBC izp|F3 TAI blk|F4 SET imp|F5 SBC zpx|F6 INC zpx|F7 SMB7 zp|F8 SED imp|F9 SBC aby|FA PLX imp|FD SBC abx|FE INC abx|FF BBS7 zpr
"""

OPCODES: list[tuple[str, str, bool]] = [("NOP?", "imp", False)] * 256  # (mnemonic, mode, defined)
for _row in _TABLE.strip().splitlines():
    for _cell in _row.split("|"):
        _op, _mn, _md = _cell.split()
        OPCODES[int(_op, 16)] = (_mn, _md, True)
assert sum(d for _, _, d in OPCODES) == 234

COND_BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}

# control-flow classes
SEQ, BRANCH, JUMP, CALL, RET, INDIRECT, HALT = "seq", "branch", "jump", "call", "ret", "indirect", "halt"


@dataclass
class Instr:
    addr: int
    opcode: int
    mnemonic: str
    mode: str
    raw: bytes
    operands: tuple[int, ...]
    target: int | None
    flow: str
    defined: bool

    @property
    def length(self) -> int:
        return len(self.raw)

    @property
    def next_addr(self) -> int:
        return (self.addr + self.length) & 0xFFFF

    def text(self, label: Callable[[int], str] | None = None) -> str:
        L = label or (lambda a: f"${a:04X}")
        o, m = self.operands, self.mode
        mn = self.mnemonic
        if m == "imp":
            return mn
        if m == "acc":
            return f"{mn} A"
        fmt = {
            "imm": lambda: f"#${o[0]:02X}",
            "zp": lambda: f"<${o[0]:02X}",
            "zpx": lambda: f"<${o[0]:02X},X",
            "zpy": lambda: f"<${o[0]:02X},Y",
            "izx": lambda: f"(<${o[0]:02X},X)",
            "izy": lambda: f"(<${o[0]:02X}),Y",
            "izp": lambda: f"(<${o[0]:02X})",
            "rel": lambda: L(self.target),
            "abs": lambda: L(o[0]) if self.flow in (JUMP, CALL) else f"${o[0]:04X}",
            "abx": lambda: f"${o[0]:04X},X",
            "aby": lambda: f"${o[0]:04X},Y",
            "ind": lambda: f"(${o[0]:04X})",
            "iax": lambda: f"(${o[0]:04X},X)",
            "zpr": lambda: f"<${o[0]:02X},{L(self.target)}",
            "imzp": lambda: f"#${o[0]:02X},<${o[1]:02X}",
            "imzpx": lambda: f"#${o[0]:02X},<${o[1]:02X},X",
            "imab": lambda: f"#${o[0]:02X},${o[1]:04X}",
            "imabx": lambda: f"#${o[0]:02X},${o[1]:04X},X",
            "blk": lambda: f"${o[0]:04X},${o[1]:04X},${o[2]:04X}",
        }[m]
        return f"{mn} {fmt()}"


def decode(read: Callable[[int], int | None], addr: int) -> Instr | None:
    """Decode one instruction at logical `addr`. `read` returns a byte or None if unmapped."""
    op = read(addr)
    if op is None:
        return None
    mn, mode, defined = OPCODES[op]
    n = MODE_LEN[mode]
    raw = [op]
    for i in range(1, n):
        b = read((addr + i) & 0xFFFF)
        if b is None:
            return None
        raw.append(b)
    w = lambda i: raw[i] | (raw[i + 1] << 8)
    s8 = lambda b: b - 256 if b & 0x80 else b

    operands: tuple[int, ...] = ()
    target = None
    flow = SEQ
    nxt = (addr + n) & 0xFFFF

    if mode in ("imm", "zp", "zpx", "zpy", "izx", "izy", "izp"):
        operands = (raw[1],)
    elif mode in ("abs", "abx", "aby", "ind", "iax"):
        operands = (w(1),)
    elif mode == "rel":
        target = (nxt + s8(raw[1])) & 0xFFFF
        operands = (raw[1],)
    elif mode == "zpr":
        target = (nxt + s8(raw[2])) & 0xFFFF
        operands = (raw[1], raw[2])
    elif mode in ("imzp", "imzpx"):
        operands = (raw[1], raw[2])
    elif mode in ("imab", "imabx"):
        operands = (raw[1], w(2))
    elif mode == "blk":
        operands = (w(1), w(3), w(5))

    if mn == "JSR":
        flow, target = CALL, operands[0]
    elif mn == "BSR":
        flow = CALL
    elif mn == "BRA":
        flow = JUMP
    elif mn == "JMP":
        if mode == "abs":
            flow, target = JUMP, operands[0]
        else:
            flow = INDIRECT
    elif mn in COND_BRANCHES or mode == "zpr":
        flow = BRANCH
    elif mn in ("RTS", "RTI"):
        flow = RET
    elif mn == "BRK":
        flow = HALT

    return Instr(addr, op, mn, mode, bytes(raw), operands, target, flow, defined)
