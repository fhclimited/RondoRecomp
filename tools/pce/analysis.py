"""Static code discovery for banked HuC6280 programs.

Memory models physical 8 KiB banks behind MPR0-7. The Explorer walks code by
recursive descent while carrying the MPR state along each path and doing light
constant propagation (A/X/Y, carry, selected zero-page globals), so bank
switches written as e.g. `LDA #n / CLC / ADC <base / TAM #$10` are followed.

Code identity is (physical bank, logical address): the same logical address
can hold different code depending on what is mapped there.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .huc6280 import BRANCH, CALL, HALT, INDIRECT, JUMP, RET, SEQ, Instr, decode
from .syscard import ZP_ARGS, bios_name

BANK_SIZE = 0x2000
MPR = tuple  # 8 bank numbers, None = unknown


class Memory:
    def __init__(self, mpr: list[int]):
        assert len(mpr) == 8
        self.mpr = list(mpr)
        self.banks: dict[int, bytearray] = {}

    def bank_of(self, addr: int, mpr=None) -> int | None:
        return (mpr or self.mpr)[(addr >> 13) & 7]

    def read_with(self, mpr, addr: int) -> int | None:
        bank = mpr[(addr >> 13) & 7]
        if bank is None:
            return None
        data = self.banks.get(bank)
        return None if data is None else data[addr & 0x1FFF]

    def read(self, addr: int) -> int | None:
        return self.read_with(self.mpr, addr)

    def load(self, addr: int, blob: bytes) -> None:
        """Write `blob` at logical `addr` through the current mapping (creates banks as needed)."""
        for i, b in enumerate(blob):
            a = (addr + i) & 0xFFFF
            bank = self.mpr[a >> 13]
            self.banks.setdefault(bank, bytearray(BANK_SIZE))[a & 0x1FFF] = b

    def snapshot(self) -> "Memory":
        m = Memory(self.mpr)
        m.banks = {k: bytearray(v) for k, v in self.banks.items()}
        return m


# --- abstract register state ----------------------------------------------

@dataclass
class State:
    A: int | None = None
    X: int | None = None
    Y: int | None = None
    C: int | None = None
    zp: dict[int, int | None] = field(default_factory=dict)
    tflag: bool = False

    def copy(self) -> "State":
        return State(self.A, self.X, self.Y, self.C, dict(self.zp), self.tflag)


_RMW = {"INC", "DEC", "ASL", "LSR", "ROL", "ROR", "TSB", "TRB"} | \
       {f"RMB{i}" for i in range(8)} | {f"SMB{i}" for i in range(8)}
_FLAG_C = {"CMP", "CPX", "CPY", "ASL", "LSR", "ROL", "ROR", "PLP", "RTI"}


def _zp_index(ins: Instr) -> int | None:
    if ins.mode == "zp":
        return ins.operands[0]
    if ins.mode == "abs" and 0x2000 <= ins.operands[0] <= 0x20FF:
        return ins.operands[0] & 0xFF
    return None


def _operand_value(ins: Instr, st: State) -> int | None:
    if ins.mode == "imm":
        return ins.operands[0]
    z = _zp_index(ins)
    return st.zp.get(z) if z is not None else None


def step(ins: Instr, st: State, globals_zp: dict[int, int]) -> None:
    """Update `st` for one instruction (data effects only; control flow handled by caller)."""
    mn, md = ins.mnemonic, ins.mode
    z = _zp_index(ins)
    if st.tflag:
        st.tflag = False
        if mn in ("ADC", "SBC", "AND", "ORA", "EOR"):
            st.zp.clear()          # T-mode ALU writes zp(X); A untouched
            return
    v = _operand_value(ins, st)
    if mn == "LDA": st.A = v
    elif mn == "LDX": st.X = v
    elif mn == "LDY": st.Y = v
    elif mn == "CLA": st.A = 0
    elif mn == "CLX": st.X = 0
    elif mn == "CLY": st.Y = 0
    elif mn == "TAX": st.X = st.A
    elif mn == "TAY": st.Y = st.A
    elif mn == "TXA": st.A = st.X
    elif mn == "TYA": st.A = st.Y
    elif mn == "SAX": st.A, st.X = st.X, st.A
    elif mn == "SAY": st.A, st.Y = st.Y, st.A
    elif mn == "SXY": st.X, st.Y = st.Y, st.X
    elif mn == "TSX": st.X = None
    elif mn in ("INX", "DEX"): st.X = None if st.X is None else (st.X + (1 if mn == "INX" else -1)) & 0xFF
    elif mn in ("INY", "DEY"): st.Y = None if st.Y is None else (st.Y + (1 if mn == "INY" else -1)) & 0xFF
    elif mn in ("INC", "DEC") and md == "acc":
        st.A = None if st.A is None else (st.A + (1 if mn == "INC" else -1)) & 0xFF
    elif mn == "CLC": st.C = 0
    elif mn == "SEC": st.C = 1
    elif mn == "SET": st.tflag = True
    elif mn == "ADC":
        if None not in (st.A, v, st.C):
            r = st.A + v + st.C
            st.A, st.C = r & 0xFF, r >> 8
        else:
            st.A = st.C = None
    elif mn == "SBC":
        if None not in (st.A, v, st.C):
            r = st.A - v - (1 - st.C)
            st.A, st.C = r & 0xFF, 0 if r < 0 else 1
        else:
            st.A = st.C = None
    elif mn in ("AND", "ORA", "EOR"):
        if None not in (st.A, v):
            st.A = {"AND": st.A & v, "ORA": st.A | v, "EOR": st.A ^ v}[mn]
        else:
            st.A = None
    elif mn in ("PLA", "TMA"):
        st.A = None
    elif mn == "PLX": st.X = None
    elif mn == "PLY": st.Y = None
    elif md == "acc":  # ASL/LSR/ROL/ROR A
        st.A = st.C = None
    if mn in ("STA", "STX", "STY", "STZ") and z is not None:
        st.zp[z] = {"STA": st.A, "STX": st.X, "STY": st.Y, "STZ": 0}[mn]
    elif mn in _RMW and z is not None:
        st.zp[z] = None
    elif mn in ("STA", "STX", "STY", "STZ") and ins.mode in ("zpx", "zpy", "izx", "izy", "izp", "abx", "aby"):
        # indexed store could hit anything in zero page except protected globals
        for k in list(st.zp):
            if k not in globals_zp:
                st.zp[k] = None
    if mn in _FLAG_C:
        st.C = None


# --- explorer ---------------------------------------------------------------

@dataclass
class Event:
    bank: int | None
    site: int
    kind: str      # bios | tam | block | indirect | unresolved | undefined | setvec
    detail: dict


@dataclass
class Explorer:
    mem: Memory
    globals_zp: dict[int, int] = field(default_factory=dict)
    max_table: int = 64

    instrs: dict[tuple[int, int], Instr] = field(default_factory=dict)
    labels: set[tuple[int, int]] = field(default_factory=set)
    entries: dict[tuple[int, int], str] = field(default_factory=dict)
    events: list[Event] = field(default_factory=list)
    _visited: set = field(default_factory=set)
    _work: list = field(default_factory=list)
    _seen_events: set = field(default_factory=set)

    def fresh_state(self) -> State:
        return State(zp=dict(self.globals_zp))

    def add_entry(self, addr: int, mpr=None, note: str = "entry") -> None:
        mpr = tuple(mpr or self.mem.mpr)
        bank = mpr[(addr >> 13) & 7]
        if bank is not None:
            self.entries.setdefault((bank, addr), note)
        self._push(addr, mpr, self.fresh_state())

    def _push(self, addr: int, mpr: tuple, st: State) -> None:
        bank = mpr[(addr >> 13) & 7]
        if bank is not None:
            self.labels.add((bank, addr))
        self._work.append((addr, mpr, st))

    def _is_bios(self, addr: int, mpr: tuple) -> bool:
        return addr >= 0xE000 and mpr[7] == 0x00 and 0x00 not in self.mem.banks

    def _ev(self, mpr, pc, kind, **detail):
        bank = mpr[(pc >> 13) & 7]
        key = (bank, pc, kind, repr(sorted(detail.items())))
        if key in self._seen_events:
            return
        self._seen_events.add(key)
        self.events.append(Event(bank, pc, kind, detail))

    def run(self) -> None:
        while self._work:
            pc, mpr, st = self._work.pop()
            self._walk(pc, mpr, st)

    def _walk(self, pc: int, mpr: tuple, st: State) -> None:
        while True:
            key = (pc, mpr)
            if key in self._visited:
                return
            if self._is_bios(pc, mpr):
                return
            bank = mpr[(pc >> 13) & 7]
            ins = decode(lambda a: self.mem.read_with(mpr, a), pc)
            if ins is None:
                self._ev(mpr, pc, "unresolved", window=pc >> 13, bank=bank)
                return
            self._visited.add(key)
            self.instrs[(bank, pc)] = ins
            if not ins.defined:
                self._ev(mpr, pc, "undefined", op=ins.opcode)
                return

            mn = ins.mnemonic
            if mn == "TAM":
                mask = ins.operands[0]
                new = list(mpr)
                for i in range(8):
                    if mask >> i & 1:
                        new[i] = st.A
                self._ev(mpr, pc, "tam", mask=mask, value=st.A)
                step(ins, st, self.globals_zp)
                mpr = tuple(new)
            elif mn == "TMA":
                mask = ins.operands[0]
                vals = {mpr[i] for i in range(8) if mask >> i & 1}
                step(ins, st, self.globals_zp)
                st.A = vals.pop() if len(vals) == 1 else None
            elif mn in ("TII", "TDD", "TIN", "TIA", "TAI"):
                s, d, n = ins.operands
                self._ev(mpr, pc, "block", op=mn, src=s, dst=d, len=n)
                step(ins, st, self.globals_zp)
            else:
                step(ins, st, self.globals_zp)

            flow, tgt = ins.flow, ins.target
            if flow == CALL:
                if self._is_bios(tgt, mpr):
                    args = {name: st.zp.get(off) for off, name in ZP_ARGS.items()}
                    name = bios_name(tgt)
                    self._ev(mpr, pc, "bios", entry=tgt, name=name, A=st.A, X=st.X, Y=st.Y, **args)
                    if name == "EX_SETVEC" and None not in (st.X, st.Y):
                        vec = st.X | st.Y << 8
                        self._ev(mpr, pc, "setvec", vector=st.A, addr=vec)
                        self.add_entry(vec, mpr, f"irq vector {st.A}")
                    keep = dict(self.globals_zp)
                    st = State(zp=keep)
                    if name == "EX_MEMOPEN":
                        st.A, st.C = 0x68, 0
                else:
                    self._push(tgt, mpr, self.fresh_state())
                    st = self.fresh_state()
                pc = ins.next_addr
                continue
            if flow == BRANCH:
                self._push(tgt, mpr, st.copy())
                pc = ins.next_addr
                b2 = mpr[(pc >> 13) & 7]
                if b2 is not None:
                    self.labels.add((b2, pc))
                continue
            if flow == JUMP:
                if self._is_bios(tgt, mpr):
                    self._ev(mpr, pc, "bios", entry=tgt, name=bios_name(tgt), tail=True)
                    return
                self._push(tgt, mpr, st)
                return
            if flow == INDIRECT:
                self._indirect(ins, mpr, st)
                return
            if flow in (RET, HALT):
                return
            pc = ins.next_addr

    def _indirect(self, ins: Instr, mpr: tuple, st: State) -> None:
        base = ins.operands[0]
        rd = lambda a: self.mem.read_with(mpr, a)
        word = lambda a: (None if rd(a) is None or rd(a + 1) is None else rd(a) | rd(a + 1) << 8)
        if ins.mode == "ind":
            t = word(base)
            # zero-page/RAM vectors are runtime-variable: do not trust their contents
            if base >= 0x4000 and t is not None:
                self._ev(mpr, ins.addr, "indirect", table=base, targets=[t], exact=True)
                self._push(t, mpr, self.fresh_state())
            else:
                self._ev(mpr, ins.addr, "indirect", table=base, targets=[], exact=False)
            return
        if st.X is not None:
            t = word(base + st.X)
            self._ev(mpr, ins.addr, "indirect", table=base, targets=[t] if t is not None else [], exact=True)
            if t is not None:
                self._push(t, mpr, self.fresh_state())
            return
        # heuristic jump-table scan: entries point into the same 16 KiB code area
        lo = ins.addr & 0xE000
        targets = []
        for k in range(self.max_table):
            t = word(base + 2 * k)
            if t is None or not (lo - 0x2000 <= t < lo + 0x4000) or t < 0x4000 or t >= 0xE000:
                break
            if rd(t) is None:
                break
            targets.append(t)
        self._ev(mpr, ins.addr, "indirect", table=base, targets=targets, exact=False)
        for t in targets:
            self._push(t, mpr, self.fresh_state())

    # --- output ------------------------------------------------------------

    def label(self, bank: int | None, addr: int) -> str:
        return f"L{bank:02X}_{addr:04X}" if bank is not None else f"L??_{addr:04X}"

    def listing(self) -> str:
        out = []
        prev = None
        for (bank, a) in sorted(self.instrs):
            ins = self.instrs[(bank, a)]
            if prev is None or prev[0] != bank:
                out.append(f"\n; ======== bank ${bank:02X} ========")
            elif prev[1] != a:
                out.append("")
            if (bank, a) in self.labels:
                note = self.entries.get((bank, a))
                out.append(f"{self.label(bank, a)}:" + (f"   ; {note}" if note else ""))

            def lab(t, _bank=bank, _a=a):
                if t >= 0xE000 and (0x00, t) not in self.labels:
                    return bios_name(t)
                # same window as this instruction -> same bank; otherwise unknown here
                if (t >> 13) == (_a >> 13):
                    return self.label(_bank, t)
                cands = [b for (b, x) in self.labels if x == t]
                return self.label(cands[0], t) if len(cands) == 1 else f"${t:04X}"

            hexb = " ".join(f"{b:02X}" for b in ins.raw)
            out.append(f"  {bank:02X}:{a:04X}  {hexb:<21s} {ins.text(lab)}")
            prev = (bank, ins.next_addr)
        return "\n".join(out) + "\n"
