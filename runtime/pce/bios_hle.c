/* System Card 3.0 functions, reimplemented in C.
 *
 * Arguments use the System Card's zero-page registers _al.._dh ($F8-$FF).
 * Each function returns an approximate CPU cycle cost; the caller performs
 * the RTS. Behaviour was established by reading what the original routines
 * do; this is an independent implementation, not a translation.
 */
#include <stdio.h>
#include <string.h>

#include "kanji_font.h"
#include "pce.h"

static const char *const names[PCE_BIOS_ENTRIES] = {
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
    "MA_ATNI", "PSG_BIOS", "GRP_BIOS", "EX_MEMOPEN", "PSG_DRIVE", "EX_COLORCMD",
    "BIOS_E0E7", "BIOS_E0EA", "BIOS_E0ED", "BIOS_E0F0",
};

enum {
    CD_BASE = 2, CD_READ = 3, CD_PLAY = 6, CD_SEARCH = 7, CD_PAUSE = 8, CD_STAT = 9, CD_FADE = 15,
    AD_RESET = 16, AD_TRANS = 17, AD_READ = 18, AD_WRITE = 19, AD_PLAY = 20, AD_CPLAY = 21, AD_STOP = 22, AD_STAT = 23,
    BM_FORMAT = 24, BM_FREE = 25, BM_READ = 26, BM_WRITE = 27, BM_DELETE = 28, BM_FILES = 29,
    EX_GETVER = 30, EX_SETVEC = 31, EX_GETFNT = 32, EX_JOYSNS = 33, EX_SCRSIZ = 35, EX_DOTMOD = 36,
    EX_SCRMOD = 37, EX_SATCLR = 54, MA_MUL8U = 63, MA_MUL8S = 64, MA_MUL16U = 65, MA_DIV16S = 66,
    MA_DIV16U = 67, EX_MEMOPEN = 74,
};

const char *bios_entry_name(int index)
{
    return index >= 0 && index < PCE_BIOS_ENTRIES ? names[index] : "?";
}

/* zero-page argument registers live in work RAM ($2000-$20FF) */
#define AL(p) ((p)->wram[0xF8])
#define AH(p) ((p)->wram[0xF9])
#define BL(p) ((p)->wram[0xFA])
#define BH(p) ((p)->wram[0xFB])
#define CL(p) ((p)->wram[0xFC])
#define CH(p) ((p)->wram[0xFD])
#define DL(p) ((p)->wram[0xFE])
#define DH(p) ((p)->wram[0xFF])

static void set_carry(Pce *p, bool c)
{
    p->cpu.p = (uint8_t)(c ? p->cpu.p | HU_FLAG_C : p->cpu.p & ~HU_FLAG_C);
}

static void vdc_set(Pce *p, uint8_t reg, uint16_t v)
{
    vdc_write(&p->vdc, 0, reg);
    vdc_write(&p->vdc, 2, (uint8_t)v);
    vdc_write(&p->vdc, 3, (uint8_t)(v >> 8));
    p->wram[0xF7] = reg;   /* the System Card keeps the selected register in zp $F7 */
}

static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static int arg_sector(Pce *p) { return CL(p) << 16 | CH(p) << 8 | DL(p); }

/* CD_READ: copy sectors from the disc to CPU memory or VRAM. */
static int cd_read(Pce *p)
{
    static uint8_t buf[256 * 2048];
    int lba = p->cd_base[0] + arg_sector(p);
    uint8_t mode = DH(p);
    uint32_t bytes;
    if (mode == 0x00 || mode == 0xFE) bytes = (uint32_t)(AL(p) | AH(p) << 8);
    else bytes = (uint32_t)AL(p) * 2048u;
    int sectors = (int)((bytes + 2047) / 2048);
    if (sectors > 256 || !disc_read_data(&p->disc, lba, sectors, buf)) {
        fprintf(stderr, "[bios] CD_READ failed: lba %d x %d (mode %02X)\n", lba, sectors, mode);
        p->cpu.a = 0x01;
        return 200;
    }
    if (mode <= 1) {
        uint16_t dst = (uint16_t)(BL(p) | BH(p) << 8);
        for (uint32_t i = 0; i < bytes; i++) pce_write8(p, (uint16_t)(dst + i), buf[i]);
    } else if (mode <= 6) {
        /* map bank _bl into MPR[mode] and fill consecutive banks through that window */
        uint8_t bank = BL(p);
        uint16_t base = (uint16_t)(mode << 13);
        for (uint32_t i = 0; i < bytes; i++) {
            if ((i & 0x1FFF) == 0) p->cpu.mpr[mode] = (uint8_t)(bank + (i >> 13));
            pce_write8(p, (uint16_t)(base + (i & 0x1FFF)), buf[i]);
        }
    } else {
        /* VRAM: write words starting at _bx */
        uint16_t addr = (uint16_t)(BL(p) | BH(p) << 8);
        for (uint32_t i = 0; i + 1 < bytes; i += 2)
            if (addr < 0x8000) p->vdc.vram[addr++] = (uint16_t)(buf[i] | buf[i + 1] << 8);
    }
    p->cpu.a = 0;
    return 1000 + sectors * 100;
}

static int cd_base(Pce *p)
{
    int lba = p->cd_base[0];
    if (BH(p) == 0) {
        for (int i = 0; i < p->disc.n_tracks; i++)
            if (p->disc.tracks[i].data) { lba = p->disc.tracks[i].lba; break; }
    } else if (BH(p) & 0x40) {
        lba = AL(p) << 16 | AH(p) << 8 | BL(p);
    } else {
        const DiscTrack *t = disc_track(&p->disc, bcd(AL(p)));
        if (t) lba = t->lba;
        else fprintf(stderr, "[bios] CD_BASE: no track %02X\n", AL(p));
    }
    if (!(CL(p) & 2)) p->cd_base[0] = lba;
    if (!(CL(p) & 1)) p->cd_base[1] = lba;
    p->cpu.a = 0;
    return 200;
}

static int ad_trans(Pce *p)
{
    static uint8_t buf[256 * 2048];
    int lba = p->cd_base[0] + arg_sector(p);
    int sectors = AL(p);
    uint16_t dst = (uint16_t)(BL(p) | BH(p) << 8);
    if (!disc_read_data(&p->disc, lba, sectors, buf)) {
        fprintf(stderr, "[bios] AD_TRANS failed: lba %d x %d\n", lba, sectors);
        p->cpu.a = 0x01;
        return 200;
    }
    for (int i = 0; i < sectors * 2048; i++) p->adpcm[(uint16_t)(dst + i)] = buf[i];
    p->cpu.a = 0;
    return 1000 + sectors * 100;
}

static int scr_mode(Pce *p)
{
    uint8_t mode = p->cpu.a & 5, w = p->cpu.x, h = p->cpu.y;
    if (w < 10 || h < 10 || h > 30) { set_carry(p, true); return 100; }
    /* horizontal: sync width 2, display start centred on the visible area */
    uint8_t hds = (uint8_t)(0x12 - (w >> 1));
    uint8_t hde = (uint8_t)(0x26 - hds - w);
    vdc_set(p, 0x0A, (uint16_t)(hds << 8 | 0x02));
    vdc_set(p, 0x0B, (uint16_t)(hde << 8 | (uint8_t)(w - 1)));
    /* vertical: centre h*8 lines inside the 240-line area */
    int lines = h * 8;
    int vds = 15 + (240 - lines) / 2;
    int vcr = PCE_LINES_PER_FRAME - 3 - (vds + 2) - lines;
    vdc_set(p, 0x0C, (uint16_t)(vds << 8 | 0x02));
    vdc_set(p, 0x0D, (uint16_t)(lines - 1));
    vdc_set(p, 0x0E, (uint16_t)(vcr < 0 ? 0 : vcr));
    vce_write(&p->vce, 0, (uint8_t)(mode & 1));
    p->mwr_shadow = (uint8_t)(p->mwr_shadow & 0x70);
    vdc_set(p, 0x09, p->mwr_shadow);
    set_carry(p, false);
    return 300;
}

/* EX_JOYSNS: refresh the BIOS joypad variables for 5 ports.
 *   joy[5]    at $2228  buttons held   (bits: 0 I, 1 II, 2 SELECT, 3 RUN, 4 up, 5 right, 6 down, 7 left)
 *   joytrg[5] at $222D  newly pressed this call
 *   joyold[5] at $2232  previous joy
 * Only port 0 has a pad (no multitap). The soft-reset combination is not implemented. */
static int joysns(Pce *p)
{
    for (int port = 0; port < 5; port++) {
        uint8_t old = p->wram[0x228 + port];
        uint8_t now = port == 0 ? p->pad : 0;
        p->wram[0x232 + port] = old;
        p->wram[0x228 + port] = now;
        p->wram[0x22D + port] = (uint8_t)((now ^ old) & now);
    }
    p->cpu.page_gen[0xF8]++;
    return 300;
}

/* CD position argument: type in bits 6-7 of the type byte.
 *   $00 LBA (hi, mid, lo)   $40 MSF in BCD (min, sec, frame)   $80 track number in BCD */
static int cd_position(Pce *p, uint8_t a, uint8_t b, uint8_t c, uint8_t type)
{
    switch (type & 0xC0) {
    case 0x00: return a << 16 | b << 8 | c;
    case 0x40: return (bcd(a) * 60 + bcd(b)) * 75 + bcd(c) - 150;
    case 0x80: {   /* a track number means the start of that track (also as an end point) */
        const DiscTrack *t = disc_track(&p->disc, bcd(a));
        return t ? t->lba : -1;
    }
    default:       /* $C0: end of the current track, handled by the caller */
        return -1;
    }
}

static const DiscTrack *audio_track_at(Pce *p, int lba)
{
    const DiscTrack *t = disc_track_at(&p->disc, lba);
    return t && !t->data ? t : NULL;
}

/* CD_PLAY: start in _al/_ah/_bl (type _bh), end in _cl/_ch/_dl (type _dh, low bits = mode:
 * 1 repeat, 2/3 play once). */
static int cd_play(Pce *p)
{
    int start = cd_position(p, AL(p), AH(p), BL(p), BH(p));
    const DiscTrack *t = start >= 0 ? audio_track_at(p, start) : NULL;
    if (!t) {
        fprintf(stderr, "[bios] CD_PLAY: start %d is not audio\n", start);
        p->cpu.a = 0;
        return 200;
    }
    int end = (DH(p) & 0xC0) == 0xC0 ? t->lba + t->sectors
                                     : cd_position(p, CL(p), CH(p), DL(p), DH(p));
    if (end <= start || end > t->lba + t->sectors) end = t->lba + t->sectors;
    p->cdda.start_lba = start;
    p->cdda.end_lba = end;
    p->cdda.pos = (int64_t)start * 588;
    p->cdda.repeat = (DH(p) & 0x07) == 1;
    p->cdda.playing = true;
    p->cdda.volume = 256;
    p->cdda.fade_step = 0;
    p->cpu.a = 0;
    return 2000;
}

/* CD_SEARCH: seek to _al/_ah/_bl (type _bh); _dh low bits: 0 pause there, else play to track end. */
static int cd_search(Pce *p)
{
    int start = cd_position(p, AL(p), AH(p), BL(p), BH(p));
    const DiscTrack *t = start >= 0 ? audio_track_at(p, start) : NULL;
    if (t) {
        p->cdda.start_lba = start;
        p->cdda.end_lba = t->lba + t->sectors;
        p->cdda.pos = (int64_t)start * 588;
        p->cdda.repeat = (DH(p) & 0x07) == 1;
        p->cdda.playing = (DH(p) & 0x07) != 0;
        p->cdda.volume = 256;
        p->cdda.fade_step = 0;
    }
    p->cpu.a = 0;
    return 2000;
}

/* CD_FADE: A = 0 cancel (full volume); 8/C fade out CD audio over about 6 s / 2.5 s. */
static int cd_fade(Pce *p)
{
    switch (p->cpu.a & 0x0F) {
    case 0x00: p->cdda.volume = 256; p->cdda.fade_step = 0; sound_adpcm_fade(&p->snd, 0); break;
    case 0x08: p->cdda.fade_step = -1; break;                /* CD audio, ~6 s */
    case 0x0C: p->cdda.fade_step = -3; break;                /* CD audio, ~2.5 s */
    case 0x0A: sound_adpcm_fade(&p->snd, -1); break;         /* ADPCM, ~6 s */
    case 0x0E: sound_adpcm_fade(&p->snd, -3); break;         /* ADPCM, ~2.5 s */
    default: break;
    }
    p->cpu.a = 0;
    return 100;
}

/* BM_READ / BM_WRITE / BM_DELETE: _ax = address of the 12-byte file name, _bx = buffer,
 * _cx = byte count, _dx = offset in the file. A = 0 on success, 1 not found, 2 bad
 * offset/size or checksum, 4 no space, $FF not formatted. BM_READ returns the count read in _cx. */
static int bm_file_op(Pce *p, int idx)
{
    uint8_t name[12];
    uint16_t na = (uint16_t)(AL(p) | AH(p) << 8);
    for (int i = 0; i < 12; i++) name[i] = pce_read8(p, (uint16_t)(na + i));
    uint16_t buf = (uint16_t)(BL(p) | BH(p) << 8);
    int len = CL(p) | CH(p) << 8, offset = DL(p) | DH(p) << 8;
    static uint8_t tmp[0x800];
    if (len > (int)sizeof tmp) len = (int)sizeof tmp;
    int r;
    if (idx == BM_READ) {
        int done = 0;
        r = bram_read(p, name, tmp, len, offset, &done);
        for (int i = 0; i < done; i++) pce_write8(p, (uint16_t)(buf + i), tmp[i]);
        CL(p) = (uint8_t)done; CH(p) = (uint8_t)(done >> 8);
    } else if (idx == BM_WRITE) {
        for (int i = 0; i < len; i++) tmp[i] = pce_read8(p, (uint16_t)(buf + i));
        r = bram_write(p, name, tmp, len, offset);
    } else {
        r = bram_delete(p, name);
    }
    p->cpu.a = (uint8_t)r;
    return 1000 + len * 4;
}

/* AD_PLAY: _bx = start in ADPCM RAM, _ax = length in bytes, _dh = rate (0-15: 32 kHz /
 * (16 - rate)), _dl: bit 7 repeat, low bits non-zero = replay the previous clip. */
static int ad_play(Pce *p)
{
    if (!(DL(p) & 0x7F)) {
        if (DH(p) >= 16) { p->cpu.a = 0x01; return 100; }
        p->ad_last_start = (uint16_t)(BL(p) | BH(p) << 8);
        p->ad_last_len = (uint16_t)(AL(p) | AH(p) << 8);
        p->ad_last_rate = DH(p);
    }
    sound_adpcm_play(&p->snd, p->master, p->ad_last_start, p->ad_last_len, p->ad_last_rate, (DL(p) & 0x80) != 0);
    p->cpu.a = 0;
    return 200;
}

/* AD_READ (ADPCM RAM -> CPU memory) / AD_WRITE (CPU memory -> ADPCM RAM): _cx = ADPCM
 * address, _ax = byte count, _dh = 0: CPU memory at _bx; 2-6: bank _bl through MPR[_dh];
 * $FF: VRAM at _bx. */
static int ad_copy(Pce *p, bool read)
{
    uint16_t ad = (uint16_t)(CL(p) | CH(p) << 8);
    uint32_t len = (uint32_t)(AL(p) | AH(p) << 8);
    uint8_t mode = DH(p);
    uint16_t dst = (uint16_t)(BL(p) | BH(p) << 8);
    if (mode == 0xFF) {                                   /* VRAM, words */
        uint16_t va = dst;
        for (uint32_t i = 0; i + 1 < len; i += 2, va++) {
            if (va >= 0x8000) break;
            if (read) {
                p->vdc.vram[va] = (uint16_t)(p->adpcm[(uint16_t)(ad + i)] | p->adpcm[(uint16_t)(ad + i + 1)] << 8);
            } else {
                p->adpcm[(uint16_t)(ad + i)] = (uint8_t)p->vdc.vram[va];
                p->adpcm[(uint16_t)(ad + i + 1)] = (uint8_t)(p->vdc.vram[va] >> 8);
            }
        }
    } else if (mode >= 2 && mode <= 6) {                  /* through a mapped bank */
        uint16_t base = (uint16_t)(mode << 13);
        for (uint32_t i = 0; i < len; i++) {
            if ((i & 0x1FFF) == 0) p->cpu.mpr[mode] = (uint8_t)(BL(p) + (i >> 13));
            uint16_t a = (uint16_t)(base + (i & 0x1FFF));
            if (read) pce_write8(p, a, p->adpcm[(uint16_t)(ad + i)]);
            else p->adpcm[(uint16_t)(ad + i)] = pce_read8(p, a);
        }
    } else {                                              /* CPU memory at _bx */
        for (uint32_t i = 0; i < len; i++) {
            if (read) pce_write8(p, (uint16_t)(dst + i), p->adpcm[(uint16_t)(ad + i)]);
            else p->adpcm[(uint16_t)(ad + i)] = pce_read8(p, (uint16_t)(dst + i));
        }
    }
    p->cpu.a = 0;
    return 500 + (int)len;
}

static void mul_result(Pce *p, uint32_t v)
{
    CL(p) = (uint8_t)v; CH(p) = (uint8_t)(v >> 8);
    DL(p) = (uint8_t)(v >> 16); DH(p) = (uint8_t)(v >> 24);
}

/* EX_GETFNT: _ax = Shift-JIS code, _bx = where the 32-byte glyph goes, _dh bit 0 set: 12x12,
 * else 16x16. The glyph is 16 rows of two bytes, high bit leftmost; a 12x12 one fills the
 * top-left corner. A = 0, or 1 for a code the System Card has no glyph for (it has JIS rows
 * 0x21-0x27 and the level-1 kanji, 0x30-0x4F). Players do not supply a System Card, so the
 * glyphs come from the bundled Shinonome font (kanji_font.c). The Japanese game draws its
 * attract-mode story with this; the English one a few words. */
static int ex_getfnt(Pce *p)
{
    int h = AH(p) - 0x81, a = AL(p) - 0x40, cell = -1;
    if (h >= 0 && h < 0x6F && !(h >= 0x1F && h < 0x3F) && a >= 0 && a < 0xBD && a != 0x3F) {
        if (h >= 0x3F) h -= 0x40;
        int row = h * 2 + 0x21, col;
        if (a < 0x3F) a++;
        if (a < 0x5F) col = a + 0x20;
        else { row++; col = a - 0x3E; }
        cell = kanji_cell(row, col);
    }
    if (cell < 0) {
        p->cpu.a = 1;
        return 200;
    }
    uint8_t g[32] = {0};
    if (DH(p) & 1) {
        const uint8_t *d = kanji_font12[cell];
        for (int k = 0; k < 6; k++, d += 3) {
            g[k * 4 + 0] = d[0];
            g[k * 4 + 1] = (uint8_t)(d[1] & 0xF0);
            g[k * 4 + 2] = (uint8_t)(d[1] << 4 | d[2] >> 4);
            g[k * 4 + 3] = (uint8_t)(d[2] << 4);
        }
    } else {
        memcpy(g, kanji_font16[cell], 32);
    }
    uint16_t dst = (uint16_t)(BL(p) | BH(p) << 8);
    for (int i = 0; i < 32; i++) pce_write8(p, (uint16_t)(dst + i), g[i]);
    p->cpu.a = 0;
    return 1200;
}

int bios_hle_call(Pce *p, int idx)
{
    if (idx < 0 || idx >= PCE_BIOS_ENTRIES) return 10;
    p->bios_calls[idx]++;
    if (p->log_bios && (p->log_bios_all || idx == CD_READ || idx == CD_BASE || idx == AD_TRANS ||
                        idx == CD_PLAY || idx == CD_SEARCH || p->bios_calls[idx] <= 3))
        fprintf(stderr, "[bios] f%06llu %-10s A=%02X X=%02X Y=%02X al=%02X ah=%02X bl=%02X bh=%02X cl=%02X ch=%02X dl=%02X dh=%02X\n",
                (unsigned long long)p->frame, names[idx], p->cpu.a, p->cpu.x, p->cpu.y,
                AL(p), AH(p), BL(p), BH(p), CL(p), CH(p), DL(p), DH(p));

    switch (idx) {
    case CD_BASE: return cd_base(p);
    case CD_READ: return cd_read(p);
    case CD_PLAY: return cd_play(p);
    case CD_SEARCH: return cd_search(p);
    case CD_PAUSE:
        p->cdda.playing = false;
        p->cpu.a = 0;
        return 200;
    case CD_FADE: return cd_fade(p);
    case CD_STAT:
        p->cpu.a = 0;               /* drive ready */
        return 100;
    case AD_RESET: case AD_STOP:
        sound_adpcm_stop(&p->snd, p->master);
        p->cpu.a = 0;
        return 100;
    case AD_PLAY: return ad_play(p);
    case AD_CPLAY: {
        /* stream from the CD: _cl/_ch/_dl = start record, _al/_ah/_bl = length in sectors,
         * _dh = rate (0-15) */
        if (DH(p) >= 16) { p->cpu.a = 0x01; return 100; }
        int lba = p->cd_base[0] + arg_sector(p);
        uint32_t sectors = (uint32_t)(AL(p) | AH(p) << 8 | BL(p) << 16);
        sound_adpcm_stream(&p->snd, p->master, lba, sectors * 2048u, DH(p));
        p->cpu.a = 0;
        return 3000;
    }
    case AD_STAT:
        p->cpu.a = p->snd.ad_playing ? 0x01 : 0x00;     /* non-zero while playing */
        return 50;
    case AD_READ: case AD_WRITE: return ad_copy(p, idx == AD_READ);
    case AD_TRANS: return ad_trans(p);

    case BM_FORMAT:
        if (!bram_formatted(p)) bram_format(p);
        p->cpu.a = 0;
        return 2000;
    case BM_FILES: {
        uint8_t name[12];
        int r = bram_file_name(p, AL(p), name);
        if (r == 0) {
            uint16_t dst = (uint16_t)(BL(p) | BH(p) << 8);
            for (int i = 0; i < 12; i++) pce_write8(p, (uint16_t)(dst + i), name[i]);
        }
        p->cpu.a = (uint8_t)r;
        return 300;
    }
    case BM_FREE: {
        int n = bram_formatted(p) ? bram_free(p) : 0;
        CL(p) = (uint8_t)n; CH(p) = (uint8_t)(n >> 8);
        p->cpu.a = bram_formatted(p) ? 0 : 0xFF;
        return 300;
    }
    case BM_READ: case BM_WRITE: case BM_DELETE: return bm_file_op(p, idx);

    case EX_GETVER:
        p->cpu.x = 3;
        p->cpu.y = 0;
        return 20;
    case EX_SETVEC: {
        uint16_t at = p->cpu.a == 6 ? 0x2284 : (uint16_t)(0x2200 + 2 * p->cpu.a);
        p->wram[at & 0x1FFF] = p->cpu.x;
        p->wram[(at + 1) & 0x1FFF] = p->cpu.y;
        p->cpu.page_gen[0xF8]++;
        return 30;
    }
    case EX_SCRSIZ:
        p->mwr_shadow = (uint8_t)((p->mwr_shadow & 0x8F) | (p->cpu.a & 7) << 4);
        vdc_set(p, 0x09, p->mwr_shadow);
        return 50;
    case EX_DOTMOD:
        p->mwr_shadow = (uint8_t)((p->mwr_shadow & 0x70) | (p->cpu.a & 0x8F));
        vdc_set(p, 0x09, p->mwr_shadow);
        return 50;
    case EX_SCRMOD: return scr_mode(p);
    case EX_SATCLR: return 100;     /* clears the BIOS's RAM sprite table, unused by this game */
    case EX_JOYSNS: return joysns(p);
    case EX_GETFNT: return ex_getfnt(p);

    case MA_MUL8U: mul_result(p, (uint32_t)AL(p) * BL(p)); return 60;
    case MA_MUL8S: mul_result(p, (uint32_t)(int32_t)((int8_t)AL(p) * (int8_t)BL(p))); return 60;
    case MA_MUL16U: mul_result(p, (uint32_t)(AL(p) | AH(p) << 8) * (uint32_t)(BL(p) | BH(p) << 8)); return 120;
    case MA_DIV16U: {
        uint16_t n = (uint16_t)(AL(p) | AH(p) << 8), d = (uint16_t)(BL(p) | BH(p) << 8);
        uint16_t q = d ? n / d : 0xFFFF, r = d ? n % d : n;
        CL(p) = (uint8_t)q; CH(p) = (uint8_t)(q >> 8); DL(p) = (uint8_t)r; DH(p) = (uint8_t)(r >> 8);
        return 200;
    }
    case MA_DIV16S: {
        int16_t n = (int16_t)(AL(p) | AH(p) << 8), d = (int16_t)(BL(p) | BH(p) << 8);
        int16_t q = d ? (int16_t)(n / d) : 0, r = d ? (int16_t)(n % d) : n;
        CL(p) = (uint8_t)q; CH(p) = (uint8_t)((uint16_t)q >> 8); DL(p) = (uint8_t)r; DH(p) = (uint8_t)((uint16_t)r >> 8);
        return 200;
    }

    case EX_MEMOPEN:
        p->cpu.a = 0x68;            /* first Super System Card RAM bank */
        p->cpu.x = 0x83;
        set_carry(p, false);
        return 50;

    default:
        if (p->bios_calls[idx] == 1)
            fprintf(stderr, "[bios] unimplemented %s (frame %llu)\n", names[idx], (unsigned long long)p->frame);
        p->cpu.a = 0;
        return 50;
    }
}
