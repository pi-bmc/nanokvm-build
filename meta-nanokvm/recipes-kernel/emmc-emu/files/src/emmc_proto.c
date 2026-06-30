// SPDX-License-Identifier: GPL-2.0
/*
 * emmc_proto.c - the eMMC card state machine and register models.
 *
 * This is the layer the ESP-IDF sdio_slave driver does NOT have: the SD/eMMC
 * *memory* semantics (CID/CSD/EXT_CSD/OCR and the CMD17/18/24/25 block model)
 * that make a U-Boot host enumerate us as storage rather than an I/O function.
 *
 * It is deliberately decision-only and allocation-free: emmc_proto_handle()
 * runs inside the PHY's IRQ-off window, so all it does is parse one captured
 * command frame and fill in a struct emmc_response (including any small CRC7).
 * The actual bit slinging happens back in emmc_phy.c.
 *
 * Register layouts and the host's parse expectations were taken from the
 * board's own U-Boot drivers/mmc/mmc.c (mmc_startup / mmc_startup_v4) and the
 * JEDEC eMMC CSD/CID field map; see README.md for the citation trail.
 */
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/kernel.h>
#else
#include <string.h>
#endif
#include "emmc_emu.h"
#include "emmc_crc.h"

/* ------------------------------------------------------------------------- */
/* 128-bit register bit-field helper (bit 127 = MSB = byte[0] bit 7)         */
/* ------------------------------------------------------------------------- */

static void reg128_set(u8 *reg, int hi, int lo, u32 val)
{
	int g;

	for (g = lo; g <= hi; g++) {
		int bit = (val >> (g - lo)) & 1;
		int byte = (127 - g) >> 3;
		int pos = g & 7;

		if (bit)
			reg[byte] |= (1u << pos);
		else
			reg[byte] &= ~(1u << pos);
	}
}

/* Append the JEDEC CRC7 into bits [7:1] of byte 15 (bit 0 stays 1). */
static void reg128_finalize_crc(u8 *reg)
{
	u8 crc = emmc_crc7(reg, 15);	/* CRC7 over the top 120 bits */

	reg[15] = (crc << 1) | 1;
}

/* ------------------------------------------------------------------------- */
/* Register models                                                           */
/* ------------------------------------------------------------------------- */

static void build_cid(struct emmc_dev *d)
{
	static const char pnm[6] = { 'N', 'K', 'E', 'M', 'M', 'C' };
	u8 *c = d->cid;
	int i;

	memset(c, 0, 16);
	reg128_set(c, 127, 120, 0x9b);		/* MID (vendor)		*/
	reg128_set(c, 113, 112, 0x1);		/* CBX = BGA		*/
	reg128_set(c, 111, 104, 0x4e);		/* OID			*/
	for (i = 0; i < 6; i++)			/* PNM "NKEMMC"		*/
		reg128_set(c, 103 - i * 8, 96 - i * 8, (u8)pnm[i]);
	reg128_set(c, 55, 48, 0x10);		/* PRV = 1.0		*/
	reg128_set(c, 47, 16, 0x12345678);	/* PSN			*/
	reg128_set(c, 15, 8, 0x32);		/* MDT			*/
	reg128_finalize_crc(c);
}

/*
 * Legacy CSD (SPEC_VERS < 4). Capacity comes entirely from C_SIZE so the host
 * never needs EXT_CSD, and a deliberately low TRAN_SPEED keeps the negotiated
 * bus clock at the host's 400 kHz floor (bcm2835 MIN_FREQ).
 *
 *   capacity_bytes = (C_SIZE + 1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN
 * with READ_BL_LEN = 9 (512) and C_SIZE_MULT = 7 (=> *512), giving
 *   capacity_bytes = (C_SIZE + 1) * 262144,  C_SIZE in [0..4095] (<= 1 GiB).
 */
static void build_csd_legacy(struct emmc_dev *d)
{
	u8 *c = d->csd;
	u32 csize;

	memset(c, 0, 16);
	csize = (u32)(d->store_bytes / 262144);
	if (csize)
		csize -= 1;
	if (csize > 4095)
		csize = 4095;

	reg128_set(c, 127, 126, 0);		/* CSD_STRUCTURE 1.0	*/
	reg128_set(c, 125, 122, d->spec_vers);	/* SPEC_VERS (3 => no EXT_CSD) */
	reg128_set(c, 119, 112, 0x0f);		/* TAAC			*/
	reg128_set(c, 111, 104, 0x01);		/* NSAC			*/
	reg128_set(c, 103, 96, 0x08);		/* TRAN_SPEED ~100kHz	*/
	reg128_set(c, 95, 84, 0x0f5);		/* CCC (0,2,4,5,6,7)	*/
	reg128_set(c, 83, 80, 9);		/* READ_BL_LEN = 512	*/
	reg128_set(c, 73, 62, csize);		/* C_SIZE		*/
	reg128_set(c, 49, 47, 7);		/* C_SIZE_MULT = 7	*/
	reg128_set(c, 46, 42, 0x1f);		/* ERASE_GRP_SIZE	*/
	reg128_set(c, 41, 37, 0x1f);		/* ERASE_GRP_MULT	*/
	reg128_set(c, 36, 32, 0x00);		/* WP_GRP_SIZE		*/
	reg128_set(c, 28, 26, 2);		/* R2W_FACTOR		*/
	reg128_set(c, 25, 22, 9);		/* WRITE_BL_LEN = 512	*/
	reg128_finalize_crc(c);

	d->capacity_blocks = (u64)(csize + 1) * 512;	/* 512-byte blocks */
	d->high_capacity = false;			/* byte addressing */
}

/*
 * v4 CSD: declares SPEC_VERS = 4 so the host then reads EXT_CSD for the real
 * (large) capacity. C_SIZE is pinned to 0xFFF (the "see EXT_CSD" sentinel).
 */
static void build_csd_v4(struct emmc_dev *d)
{
	u8 *c = d->csd;

	memset(c, 0, 16);
	reg128_set(c, 127, 126, 0);
	reg128_set(c, 125, 122, 4);		/* SPEC_VERS = 4 (eMMC 4.x) */
	reg128_set(c, 119, 112, 0x0f);
	reg128_set(c, 111, 104, 0x01);
	reg128_set(c, 103, 96, 0x08);		/* low TRAN_SPEED	*/
	reg128_set(c, 95, 84, 0x0f5);
	reg128_set(c, 83, 80, 9);
	reg128_set(c, 73, 62, 0xfff);		/* C_SIZE => use EXT_CSD	*/
	reg128_set(c, 49, 47, 7);
	reg128_set(c, 46, 42, 0x1f);
	reg128_set(c, 41, 37, 0x1f);
	reg128_set(c, 28, 26, 2);
	reg128_set(c, 25, 22, 9);
	reg128_finalize_crc(c);
}

/* EXT_CSD (512 bytes). Only the fields U-Boot reads are populated; the rest is
 * zero. CARD_TYPE/HS_TIMING are pinned to legacy so the host keeps the bus in
 * backward-compatible timing (no high-speed switch, clock stays at the floor).
 */
static void build_ext_csd(struct emmc_dev *d)
{
	u8 *e = d->ext_csd;
	u64 sec = d->capacity_blocks;

	memset(e, 0, 512);
	e[183] = 0x00;				/* BUS_WIDTH = 1-bit	*/
	e[185] = 0x00;				/* HS_TIMING = legacy	*/
	e[192] = 5;				/* EXT_CSD_REV = 4.41	*/
	e[194] = 1;				/* CSD_STRUCTURE	*/
	e[196] = 0x01;				/* CARD_TYPE = 26MHz only */
	/* SEC_COUNT @212 little-endian (valid only when device > 2GB). */
	e[212] = sec & 0xff;
	e[213] = (sec >> 8) & 0xff;
	e[214] = (sec >> 16) & 0xff;
	e[215] = (sec >> 24) & 0xff;
	e[226] = 0x57;				/* BOOT_SIZE_MULT (cosmetic) */
}

static void build_ocr(struct emmc_dev *d)
{
	d->ocr = OCR_VDD_27_36 | OCR_VDD_170_195;
	if (!emmc_force_legacy)
		d->ocr |= OCR_ACCESS_SECTOR;	/* sector addressing (>2GB) */
}

void emmc_proto_init_registers(struct emmc_dev *d)
{
	/*
	 * Mode select. Legacy (default) is the most robust "fool U-Boot"
	 * path: SPEC_VERS<4 means no EXT_CSD data transfer is needed to be
	 * detected. v4 mode is required for >1 GiB but depends on the data
	 * phase working (see README "single-core caveat").
	 */
	if (emmc_force_legacy || d->store_bytes <= (1ULL << 30)) {
		d->spec_vers = 3;
		build_ocr(d);
		build_csd_legacy(d);
	} else {
		d->spec_vers = 4;
		d->capacity_blocks = d->store_bytes / 512;
		d->high_capacity = true;
		build_ocr(d);
		build_csd_v4(d);
		build_ext_csd(d);
	}
	build_cid(d);
	d->block_len = EMMC_BLOCK_LEN;
}

void emmc_proto_reset(struct emmc_dev *d)
{
	d->state = ST_IDLE;
	d->selected = false;
	d->powered_up = false;
	d->predef_blocks = 0;
}

/* ------------------------------------------------------------------------- */
/* Response framing                                                          */
/* ------------------------------------------------------------------------- */

struct bitw { u8 *buf; int pos; };

static inline void bw_bit(struct bitw *w, u32 b)
{
	if (b)
		w->buf[w->pos >> 3] |= 1u << (7 - (w->pos & 7));
	w->pos++;
}

static inline void bw_bits(struct bitw *w, u32 val, int n)
{
	while (n--)
		bw_bit(w, (val >> n) & 1);
}

static u32 card_status(struct emmc_dev *d)
{
	return R1_CURRENT_STATE(d->state) | R1_READY_FOR_DATA;
}

static void frame_r1(struct emmc_dev *d, struct emmc_response *r, u8 idx,
		     u32 status)
{
	struct bitw w = { r->bits, 0 };
	u8 crc;

	memset(r->bits, 0, sizeof(r->bits));
	bw_bit(&w, 0);				/* start	*/
	bw_bit(&w, 0);				/* transmission	*/
	bw_bits(&w, idx, 6);			/* command index echo */
	bw_bits(&w, status, 32);		/* card status	*/
	crc = emmc_crc7(r->bits, 5);		/* CRC7 over the first 40 bits */
	bw_bits(&w, crc, 7);
	bw_bit(&w, 1);				/* end		*/
	r->nbits = 48;
}

static void frame_r3(struct emmc_dev *d, struct emmc_response *r, u32 ocr)
{
	struct bitw w = { r->bits, 0 };

	memset(r->bits, 0, sizeof(r->bits));
	bw_bit(&w, 0);
	bw_bit(&w, 0);
	bw_bits(&w, 0x3f, 6);			/* reserved (no index)	*/
	bw_bits(&w, ocr, 32);
	bw_bits(&w, 0x7f, 7);			/* CRC field = 1111111	*/
	bw_bit(&w, 1);
	r->nbits = 48;
}

static void frame_r2(struct emmc_dev *d, struct emmc_response *r, const u8 *reg)
{
	struct bitw w = { r->bits, 0 };
	int i;

	memset(r->bits, 0, sizeof(r->bits));
	bw_bit(&w, 0);				/* start	*/
	bw_bit(&w, 0);				/* transmission	*/
	bw_bits(&w, 0x3f, 6);			/* reserved	*/
	for (i = 0; i < 15; i++)		/* reg[127:8]	*/
		bw_bits(&w, reg[i], 8);
	bw_bits(&w, reg[15] >> 1, 7);		/* reg[7:1] (its CRC7)	*/
	bw_bit(&w, 1);				/* end		*/
	r->nbits = 136;
}

/* ------------------------------------------------------------------------- */
/* Command dispatch                                                          */
/* ------------------------------------------------------------------------- */

static u64 arg_to_block(struct emmc_dev *d, u32 arg)
{
	return d->high_capacity ? arg : (arg / EMMC_BLOCK_LEN);
}

bool emmc_proto_handle(struct emmc_dev *d, const u8 *frame,
		       struct emmc_response *r)
{
	u8 idx = frame[0] & 0x3f;
	u32 arg = ((u32)frame[1] << 24) | ((u32)frame[2] << 16) |
		  ((u32)frame[3] << 8) | frame[4];

	memset(r, 0, sizeof(*r));
	r->kind = RESP_NONE;
	r->data_dir = DATA_NONE;
	r->nblocks = 0;
	d->last_cmd = idx;
	d->last_arg = arg;

	switch (idx) {
	case CMD_GO_IDLE_STATE:			/* CMD0 */
		emmc_proto_reset(d);
		break;

	case CMD_SEND_OP_COND:			/* CMD1 (eMMC) */
		/* Report ready immediately: busy bit set, voltage window. */
		d->powered_up = true;
		d->state = ST_READY;
		frame_r3(d, r, d->ocr | OCR_BUSY);
		r->kind = RESP_R3;
		break;

	case CMD_ALL_SEND_CID:			/* CMD2 */
		if (d->state == ST_READY)
			d->state = ST_IDENT;
		frame_r2(d, r, d->cid);
		r->kind = RESP_R2;
		break;

	case CMD_SET_RELATIVE_ADDR:		/* CMD3 (host assigns RCA) */
		d->rca = arg >> 16;
		d->state = ST_STBY;
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		break;

	case CMD_SEND_CSD:			/* CMD9 */
		if ((arg >> 16) == d->rca) {
			frame_r2(d, r, d->csd);
			r->kind = RESP_R2;
		} else {
			frame_r1(d, r, idx,
				 card_status(d) | R1_ILLEGAL_COMMAND);
			r->kind = RESP_R1;
		}
		break;

	case CMD_SEND_CID:			/* CMD10 */
		frame_r2(d, r, d->cid);
		r->kind = RESP_R2;
		break;

	case CMD_SELECT_CARD:			/* CMD7 */
		if ((arg >> 16) == d->rca && d->rca != 0) {
			d->selected = true;
			d->state = ST_TRAN;
			frame_r1(d, r, idx, card_status(d));
			r->kind = RESP_R1B;
		} else {
			/* Deselect (RCA 0): no response on the bus. */
			d->selected = false;
			d->state = ST_STBY;
			r->kind = RESP_NONE;
		}
		break;

	case CMD_SEND_EXT_CSD:			/* CMD8 (data: 512B EXT_CSD) */
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		r->data_dir = DATA_TO_HOST;
		r->nblocks = 1;
		r->fixed_block = d->ext_csd;
		break;

	case CMD_SWITCH:			/* CMD6 (EXT_CSD write) */
		/* arg: [25:24]=access [23:16]=index [15:8]=value. Apply to our
		 * EXT_CSD model so a subsequent read-back is consistent. */
		if (((arg >> 24) & 3) == 3) {	/* write byte */
			u8 ei = (arg >> 16) & 0xff;
			u8 ev = (arg >> 8) & 0xff;

			d->ext_csd[ei] = ev;
		}
		d->state = ST_PRG;
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1B;
		d->state = ST_TRAN;
		break;

	case CMD_SEND_STATUS:			/* CMD13 */
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		break;

	case CMD_SET_BLOCKLEN:			/* CMD16 */
		d->block_len = arg ? arg : EMMC_BLOCK_LEN;
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		break;

	case CMD_SET_BLOCK_COUNT:		/* CMD23 */
		d->predef_blocks = arg & 0xffff;
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		break;

	case CMD_READ_SINGLE_BLOCK:		/* CMD17 */
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		r->data_dir = DATA_TO_HOST;
		r->nblocks = 1;
		r->block_addr = arg_to_block(d, arg);
		d->state = ST_DATA;
		break;

	case CMD_READ_MULTIPLE_BLOCK:		/* CMD18 */
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		r->data_dir = DATA_TO_HOST;
		r->block_addr = arg_to_block(d, arg);
		if (d->predef_blocks) {
			r->nblocks = d->predef_blocks;
			r->open_ended = false;
		} else {
			r->nblocks = 0;
			r->open_ended = true;	/* until CMD12	*/
		}
		d->state = ST_DATA;
		break;

	case CMD_WRITE_BLOCK:			/* CMD24 */
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		r->data_dir = DATA_FROM_HOST;
		r->nblocks = 1;
		r->block_addr = arg_to_block(d, arg);
		d->state = ST_RCV;
		break;

	case CMD_WRITE_MULTIPLE_BLOCK:		/* CMD25 */
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1;
		r->data_dir = DATA_FROM_HOST;
		r->block_addr = arg_to_block(d, arg);
		if (d->predef_blocks) {
			r->nblocks = d->predef_blocks;
			r->open_ended = false;
		} else {
			r->nblocks = 0;
			r->open_ended = true;
		}
		d->state = ST_RCV;
		break;

	case CMD_STOP_TRANSMISSION:		/* CMD12 */
		d->state = ST_TRAN;
		frame_r1(d, r, idx, card_status(d));
		r->kind = RESP_R1B;
		break;

	case CMD_SET_DSR:			/* CMD4: no response */
		break;

	default:
		frame_r1(d, r, idx, card_status(d) | R1_ILLEGAL_COMMAND);
		r->kind = RESP_R1;
		break;
	}

	d->predef_blocks = (idx == CMD_SET_BLOCK_COUNT) ? d->predef_blocks : 0;
	return true;
}

/* ------------------------------------------------------------------------- */
/* Backing-store block access                                                */
/* ------------------------------------------------------------------------- */

int emmc_store_read(struct emmc_dev *d, u64 block, u8 *buf512)
{
	u64 off = block * EMMC_BLOCK_LEN;

	if (!d->store || off + EMMC_BLOCK_LEN > d->store_bytes) {
		memset(buf512, 0xff, EMMC_BLOCK_LEN);
		return -ERANGE;
	}
	memcpy(buf512, d->store + off, EMMC_BLOCK_LEN);
	return 0;
}

int emmc_store_write(struct emmc_dev *d, u64 block, const u8 *buf512)
{
	u64 off = block * EMMC_BLOCK_LEN;

	if (!d->store || off + EMMC_BLOCK_LEN > d->store_bytes)
		return -ERANGE;
	memcpy(d->store + off, buf512, EMMC_BLOCK_LEN);
	return 0;
}
