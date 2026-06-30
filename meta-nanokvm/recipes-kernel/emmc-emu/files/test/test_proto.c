// SPDX-License-Identifier: GPL-2.0
/*
 * test_proto.c - host self-test for the REAL emmc_proto.c.
 *
 * Builds the card registers exactly as the module does, then parses them with
 * U-Boot's own drivers/mmc/mmc.c formulas to prove a bcm2835 host would extract
 * the right version (<4, so no EXT_CSD), capacity, a sub-400kHz clock, and
 * valid CRC7s. Also exercises emmc_proto_handle() response framing.
 *
 *   gcc -I../src -I. test_proto.c ../src/emmc_proto.c ../src/emmc_crc.c -o t
 */
#include <stdio.h>
#include "emmc_emu.h"
#include "emmc_crc.h"

/* extern referenced by emmc_proto.c */
bool emmc_force_legacy = true;
uint emmc_capacity_mb = 256;
uint emmc_clk_spin = 200000;

static int fails;
#define CHECK(cond, ...) do { \
	if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } \
	else { printf("  ok:   " __VA_ARGS__); printf("\n"); } \
} while (0)

/* ---- U-Boot drivers/mmc/mmc.c parse, transcribed ---- */
static const int fbase[] = { 10000, 100000, 1000000, 10000000 };
static const int mult_tbl[] = {
	0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80
};

static u32 word(const u8 *csd, int w) /* big-endian 32-bit, MSB-first */
{
	const u8 *p = csd + w * 4;

	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void parse_csd_and_check(struct emmc_dev *d)
{
	u32 r0 = word(d->csd, 0), r1 = word(d->csd, 1), r2 = word(d->csd, 2);
	int version = (r0 >> 26) & 0xf;
	int freq = fbase[r0 & 0x7];
	int mult = mult_tbl[(r0 >> 3) & 0xf];
	long long legacy = (long long)freq * mult;
	int read_bl_len = 1 << ((r1 >> 16) & 0xf);
	u32 csize = (r1 & 0x3ff) << 2 | (r2 & 0xc0000000) >> 30;
	u32 cmult = (r2 & 0x00038000) >> 15;
	unsigned long long blocknr = (unsigned long long)(csize + 1) << (cmult + 2);
	unsigned long long capacity = blocknr * read_bl_len;
	u8 csd_crc = emmc_crc7(d->csd, 15);

	printf("[CSD] version=%d legacy_clk=%lldHz read_bl_len=%d csize=%u cmult=%u\n",
	       version, legacy, read_bl_len, csize, cmult);
	CHECK(version < 4, "SPEC_VERS<4 => host skips EXT_CSD (version=%d)", version);
	CHECK(legacy <= 400000, "TRAN_SPEED clamps to host 400kHz floor (%lldHz)", legacy);
	CHECK(read_bl_len == 512, "READ_BL_LEN == 512");
	CHECK(capacity == d->store_bytes,
	      "capacity %llu == store %llu", capacity, (unsigned long long)d->store_bytes);
	CHECK((d->csd[15] >> 1) == csd_crc, "CSD CRC7 embedded == computed (0x%02x)", csd_crc);
}

static void check_cid(struct emmc_dev *d)
{
	u8 cid_crc = emmc_crc7(d->cid, 15);

	CHECK((d->cid[15] >> 1) == cid_crc, "CID CRC7 embedded == computed (0x%02x)", cid_crc);
	CHECK(d->cid[0] == 0x9b, "CID MID byte present");
}

static void mkcmd(u8 *f, u8 idx, u32 arg)
{
	f[0] = 0x40 | (idx & 0x3f);	/* start=0, tx=1, idx */
	f[1] = arg >> 24; f[2] = arg >> 16; f[3] = arg >> 8; f[4] = arg;
	f[5] = (emmc_crc7(f, 5) << 1) | 1;
}

static void check_handle(struct emmc_dev *d)
{
	struct emmc_response r;
	u8 f[6];

	/* CMD1 -> R3, "command index" field is reserved 1s (0x3f). */
	mkcmd(f, CMD_SEND_OP_COND, 0);
	emmc_proto_handle(d, f, &r);
	CHECK(r.kind == RESP_R3 && r.nbits == 48 && r.bits[0] == 0x3f,
	      "CMD1 -> R3 (48b, OCR busy=%d)", !!(((u32)r.bits[1] << 24) & OCR_BUSY));

	/* CMD2 -> R2 (136b), first payload byte == CID[0]. */
	mkcmd(f, CMD_ALL_SEND_CID, 0);
	emmc_proto_handle(d, f, &r);
	CHECK(r.kind == RESP_R2 && r.nbits == 136 && r.bits[0] == 0x3f &&
	      r.bits[1] == d->cid[0], "CMD2 -> R2 (136b, CID streamed)");

	/* CMD3 sets RCA; R1 echoes idx and carries a valid CRC7. */
	mkcmd(f, CMD_SET_RELATIVE_ADDR, 0x00010000);
	emmc_proto_handle(d, f, &r);
	CHECK(r.kind == RESP_R1 && r.nbits == 48 && r.bits[0] == CMD_SET_RELATIVE_ADDR &&
	      (r.bits[5] >> 1) == emmc_crc7(r.bits, 5) && d->rca == 1,
	      "CMD3 -> R1 idx echo+CRC, RCA=1");

	/* CMD9 to the assigned RCA -> R2 (CSD). */
	mkcmd(f, CMD_SEND_CSD, 0x00010000);
	emmc_proto_handle(d, f, &r);
	CHECK(r.kind == RESP_R2 && r.nbits == 136 && r.bits[1] == d->csd[0],
	      "CMD9(RCA) -> R2 (CSD streamed)");

	/* CMD7 select -> R1b. */
	mkcmd(f, CMD_SELECT_CARD, 0x00010000);
	emmc_proto_handle(d, f, &r);
	CHECK(r.kind == RESP_R1B && d->state == ST_TRAN && d->selected,
	      "CMD7(RCA) -> R1b, state=tran");

	/* Unknown command -> R1 with ILLEGAL_COMMAND. */
	mkcmd(f, 38, 0);
	emmc_proto_handle(d, f, &r);
	CHECK(r.kind == RESP_R1 &&
	      (((u32)r.bits[1] << 24 | (u32)r.bits[2] << 16 |
		(u32)r.bits[3] << 8 | r.bits[4]) & R1_ILLEGAL_COMMAND),
	      "CMD38 (unknown) -> R1 ILLEGAL_COMMAND");
}

int main(void)
{
	struct emmc_dev d;
	unsigned long long sizes[] = { 64ULL << 20, 256ULL << 20, 512ULL << 20,
				       1024ULL << 20 };
	int i;

	for (i = 0; i < 4; i++) {
		memset(&d, 0, sizeof(d));
		d.store_bytes = sizes[i];
		emmc_proto_init_registers(&d);
		emmc_proto_reset(&d);
		printf("\n=== capacity %llu MiB (legacy) ===\n", sizes[i] >> 20);
		parse_csd_and_check(&d);
		check_cid(&d);
		check_handle(&d);
	}

	printf("\n%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED",
	       fails);
	return fails ? 1 : 0;
}
