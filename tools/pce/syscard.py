"""System Card (CD-ROM² BIOS) interface: entry-point jump table and argument registers.

The BIOS lives in physical bank $00, mapped at $E000-$FFFF (MPR7 = $00). Games call
it through a jump table of 3-byte JMPs starting at $E000. Names follow Hudson's
documentation as used by the HuC/MagicKit headers. They still need checking
against a System Card 3.0 dump or traces; treat as provisional until then.
"""

ENTRY_NAMES = [
    "CD_BOOT", "CD_RESET", "CD_BASE", "CD_READ", "CD_SEEK", "CD_EXEC", "CD_PLAY", "CD_SEARCH",
    "CD_PAUSE", "CD_STAT", "CD_SUBQ", "CD_DINFO", "CD_CONTNTS", "CD_SUBRD", "CD_PCMRD", "CD_FADE",
    "AD_RESET", "AD_TRANS", "AD_READ", "AD_WRITE", "AD_PLAY", "AD_CPLAY", "AD_STOP", "AD_STAT",
    "BM_FORMAT", "BM_FREE", "BM_READ", "BM_WRITE", "BM_DELETE", "BM_FILES",
    "EX_GETVER", "EX_SETVEC", "EX_GETFNT", "EX_JOYSNS", "EX_JOYREP", "EX_SCRSIZ", "EX_DOTMOD",
    "EX_SCRMOD", "EX_IMODE", "EX_VMODE", "EX_HMODE", "EX_VSYNC", "EX_RCRON", "EX_RCROFF",
    "EX_IRQON", "EX_IRQOFF", "EX_BGON", "EX_BGOFF", "EX_SPRON", "EX_SPROFF", "EX_DSPON",
    "EX_DSPOFF", "EX_DMAMOD", "EX_SPRDMA", "EX_SATCLR", "EX_SPRPUT", "EX_SETRCR", "EX_SETRED",
    "EX_SETWRT", "EX_SETDMA", "EX_BINBCD", "EX_BCDBIN", "EX_RND",
    "MA_MUL8U", "MA_MUL8S", "MA_MUL16U", "MA_DIV16S", "MA_DIV16U", "MA_SQRT", "MA_SIN", "MA_COS",
    "MA_ATNI", "PSG_BIOS", "GRP_BIOS",
    # System Card 3.0: returns first Super RAM bank in A, carry clear if present
    # (confirmed by Rondo's boot code, which maps the returned bank and its successors)
    "EX_MEMOPEN",
    "PSG_DRIVE", "EX_COLORCMD",
]

BIOS_ENTRIES = {0xE000 + 3 * i: name for i, name in enumerate(ENTRY_NAMES)}


def bios_name(addr: int) -> str:
    return BIOS_ENTRIES.get(addr, f"BIOS_{addr:04X}")


# Verified against System Card 3.0 (Japan, md5 38179df8...): the jump table has 81
# entries; the last 4 ($E0E7-$E0F0) are unnamed here and unused by Rondo.
JUMP_TABLE_ENTRIES = 81

# Interrupt dispatch. Each hardware vector handler first tests one bit of IRQ_MODE
# (zp $F5, "irq_m"); if set it jumps straight through the work-RAM vector and the
# BIOS does no processing of its own for that interrupt.
IRQ_MODE = 0xF5
RAM_VECTORS = {           # irq_m bit -> (vector address, use)
    0: (0x2200, "IRQ2 (CD-ROM / ADPCM)"),
    1: (0x2202, "IRQ1 (VDC)"),
    2: (0x2204, "TIMER"),
    3: (0x2206, "NMI"),
    4: (0x2208, "vsync hook, called by the BIOS IRQ1 handler"),
    6: (0x220A, "hsync hook, called by the BIOS IRQ1 handler"),
}

# Zero-page argument registers ($2000-$20FF is zero page through MPR1 = $F8)
ZP_ARGS = {
    0xF8: "_al", 0xF9: "_ah", 0xFA: "_bl", 0xFB: "_bh",
    0xFC: "_cl", 0xFD: "_ch", 0xFE: "_dl", 0xFF: "_dh",
}

# Physical bank ranges on a Super CD-ROM² system
BANK_SYSCARD_ROM = range(0x00, 0x20)   # System Card 3.0 ROM (256 KiB)
BANK_SUPER_RAM = range(0x68, 0x80)     # Super System Card RAM (192 KiB)
BANK_CD_RAM = range(0x80, 0x88)        # CD-ROM² base RAM (64 KiB)
BANK_BRAM = 0xF7                       # backup RAM
BANK_WRAM = 0xF8                       # 8 KiB work RAM (zero page + stack)
BANK_IO = 0xFF                         # hardware I/O


def bank_kind(bank: int) -> str:
    if bank in BANK_SYSCARD_ROM:
        return "syscard"
    if bank in BANK_SUPER_RAM:
        return "super-ram"
    if bank in BANK_CD_RAM:
        return "cd-ram"
    return {BANK_BRAM: "bram", BANK_WRAM: "wram", BANK_IO: "io"}.get(bank, "unmapped")
