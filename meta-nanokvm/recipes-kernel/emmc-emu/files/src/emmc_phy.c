// SPDX-License-Identifier: GPL-2.0
/*
 * emmc_phy.c - the bit-level eMMC card PHY (software shift register).
 *
 * This is the part the ESP32-C6 does in dedicated silicon. Here it runs on the
 * CPU, so every routine below is a tight MMIO poll loop and MUST be called with
 * local interrupts disabled (the caller in emmc_main.c owns that window).
 *
 * Clocking model (SD/eMMC default speed):
 *   - The host owns CLK; we never drive it. We recover bit timing by polling
 *     the CLK pad and detecting edges.
 *   - RECEIVE  (host -> card): host data is stable around the CLK *rising*
 *     edge, so we sample CMD/DAT on the rising edge.
 *   - TRANSMIT (card -> host): we update our outputs on the CLK *falling* edge
 *     so the level is settled across the whole high phase and the host latches
 *     it cleanly on the next rising edge.
 *
 * Frame formats:
 *   command : 48 bits  [start=0][tx=1][idx:6][arg:32][crc7:7][end=1]
 *   R1/R3   : 48 bits  [start=0][tx=0][idx|0x3f:6][payload:32][crc7|1111111:7][end=1]
 *   R2      : 136 bits [start=0][tx=0][0x3f:6][CID/CSD:120][crc7:7][end=1]
 *   data    : [start=0][512 bytes][crc16:16 per line][end=1]
 */
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/nmi.h>
#include "emmc_emu.h"
#include "emmc_io.h"
#include "emmc_crc.h"

/* Per-edge spin budget (number of MMIO samples). Tunable via module param. */
#define EDGE_BUDGET	(emmc_clk_spin)

/* Ncr: clocks between a command's end bit and the response start bit (>= 2). */
#define NCR_GAP		2
/* Nac-ish gap before read data after the response. */
#define NAC_GAP		4

/* ------------------------------------------------------------------------- */
/* Edge primitives                                                           */
/* ------------------------------------------------------------------------- */

/* Spin until a CLK rising edge; return the sampled bus word, or -ETIMEDOUT. */
static __always_inline int wait_rise(struct emmc_dev *d, u32 *raw, u32 budget)
{
	u32 prev = io_clk(io_sample(d));

	while (budget--) {
		u32 r = io_sample(d);
		u32 c = io_clk(r);

		if (!prev && c) {
			*raw = r;
			return 0;
		}
		prev = c;
	}
	return -ETIMEDOUT;
}

/* Spin until a CLK falling edge. */
static __always_inline int wait_fall(struct emmc_dev *d, u32 *raw, u32 budget)
{
	u32 prev = io_clk(io_sample(d));

	while (budget--) {
		u32 r = io_sample(d);
		u32 c = io_clk(r);

		if (prev && !c) {
			*raw = r;
			return 0;
		}
		prev = c;
	}
	return -ETIMEDOUT;
}

void emmc_phy_setup(struct emmc_dev *d)
{
	/* Everything tri-stated; host drives CLK/CMD/DAT at reset. */
	emmc_gpio_idle(d);
}

/* ------------------------------------------------------------------------- */
/* Command receive                                                           */
/* ------------------------------------------------------------------------- */

/*
 * Wait for and capture one 48-bit command frame.
 *
 * @spin_budget bounds the hunt for a start bit so the caller can periodically
 * re-open the interrupt window when the bus is idle. Once a start bit is found
 * we commit to clocking in all 48 bits (each bit bounded by EDGE_BUDGET; if the
 * host stops mid-frame we abort and resync).
 *
 * Returns 1 on a captured frame, 0 on idle timeout, <0 on a framing error.
 */
int emmc_phy_recv_command(struct emmc_dev *d, u8 *frame, u32 spin_budget)
{
	bool seen_idle = false;
	u32 raw, prev;
	int i;

	frame[0] = frame[1] = frame[2] = 0;
	frame[3] = frame[4] = frame[5] = 0;

	/* Hunt for the start bit: first rising edge with CMD low after idle. */
	prev = io_clk(io_sample(d));
	while (spin_budget--) {
		u32 r = io_sample(d);
		u32 c = io_clk(r);

		if (!prev && c) {		/* rising edge */
			u32 cmd = io_cmd(r);

			if (!seen_idle) {
				if (cmd)	/* CMD high => bus is idle */
					seen_idle = true;
			} else if (!cmd) {	/* start bit (0) */
				goto capture;
			}
		}
		prev = c;
	}
	return 0;				/* idle: let caller breathe */

capture:
	/* bit 0 is the start bit (0); clock in bits 1..47 on rising edges. */
	for (i = 1; i < 48; i++) {
		u32 bit;

		if (wait_rise(d, &raw, EDGE_BUDGET))
			return -EIO;		/* clock stalled mid-frame */
		bit = io_cmd(raw);
		frame[i >> 3] |= bit << (7 - (i & 7));
	}
	return 1;
}

/* ------------------------------------------------------------------------- */
/* Response transmit                                                         */
/* ------------------------------------------------------------------------- */

void emmc_phy_send_response(struct emmc_dev *d, const struct emmc_response *r)
{
	u32 raw;
	int i;

	if (r->kind == RESP_NONE)
		return;

	io_cmd_output(d);

	/* Ncr gap: let a couple of clocks pass before driving the start bit. */
	for (i = 0; i < NCR_GAP; i++)
		if (wait_fall(d, &raw, EDGE_BUDGET))
			goto release;

	/* Drive each response bit on the falling edge, MSB first. */
	for (i = 0; i < r->nbits; i++) {
		u32 bit = (r->bits[i >> 3] >> (7 - (i & 7))) & 1;

		if (wait_fall(d, &raw, EDGE_BUDGET))
			goto release;
		io_cmd_drive(d, bit);
	}

	/* One more falling edge so the end bit is clocked, then hand CMD back. */
	wait_fall(d, &raw, EDGE_BUDGET);

release:
	io_cmd_release(d);

	/*
	 * R1b: assert busy on DAT0 (drive low) briefly, then release. The
	 * bcm2835 host sets SDHCI_QUIRK_BROKEN_R1B and does not poll this
	 * closely, so a short token is sufficient.
	 */
	if (r->kind == RESP_R1B) {
		io_dat_output(d, BIT_D0);
		io_dat0_drive(d, 0);
		for (i = 0; i < 8; i++)
			if (wait_fall(d, &raw, EDGE_BUDGET))
				break;
		io_dat_release(d);
	}
}

/* ------------------------------------------------------------------------- */
/* Data block transmit (read) - 1-bit mode, DAT0                             */
/* ------------------------------------------------------------------------- */

int emmc_phy_send_data_block(struct emmc_dev *d, const u8 *buf512)
{
	u16 crc = emmc_crc16_init();
	u32 raw;
	int i, b;

	io_dat_output(d, BIT_D0);

	/* Nac gap before the start bit. */
	for (i = 0; i < NAC_GAP; i++)
		if (wait_fall(d, &raw, EDGE_BUDGET))
			return -EIO;

	/* Start bit = 0. */
	if (wait_fall(d, &raw, EDGE_BUDGET))
		return -EIO;
	io_dat0_drive(d, 0);

	/* 512 payload bytes, MSB first, folding CRC16 as we go. */
	for (i = 0; i < EMMC_BLOCK_LEN; i++) {
		u8 byte = buf512[i];

		for (b = 7; b >= 0; b--) {
			u32 bit = (byte >> b) & 1;

			if (wait_fall(d, &raw, EDGE_BUDGET))
				return -EIO;
			io_dat0_drive(d, bit);
			crc = emmc_crc16_bit(crc, bit);
		}
	}

	/* 16-bit CRC, MSB first. */
	for (b = 15; b >= 0; b--) {
		u32 bit = (crc >> b) & 1;

		if (wait_fall(d, &raw, EDGE_BUDGET))
			return -EIO;
		io_dat0_drive(d, bit);
	}

	/* End bit = 1, then release. */
	if (wait_fall(d, &raw, EDGE_BUDGET))
		return -EIO;
	io_dat0_drive(d, 1);
	wait_fall(d, &raw, EDGE_BUDGET);
	io_dat_release(d);

	touch_softlockup_watchdog();
	return 0;
}

/* ------------------------------------------------------------------------- */
/* Data block receive (write) - 1-bit mode, DAT0                             */
/* ------------------------------------------------------------------------- */

int emmc_phy_recv_data_block(struct emmc_dev *d, u8 *buf512)
{
	u16 crc = emmc_crc16_init(), host_crc = 0;
	u32 raw;
	int i, b;

	/* Wait for the host's start bit (DAT0 low at a rising edge). */
	for (i = 0; ; i++) {
		if (wait_rise(d, &raw, EDGE_BUDGET))
			return -EIO;
		if (!io_dat0(raw))
			break;
		if (i > 4096)			/* host never started */
			return -ETIMEDOUT;
	}

	for (i = 0; i < EMMC_BLOCK_LEN; i++) {
		u8 byte = 0;

		for (b = 7; b >= 0; b--) {
			u32 bit;

			if (wait_rise(d, &raw, EDGE_BUDGET))
				return -EIO;
			bit = io_dat0(raw);
			byte |= bit << b;
			crc = emmc_crc16_bit(crc, bit);
		}
		buf512[i] = byte;
	}

	/* Read the host's 16-bit CRC. */
	for (b = 15; b >= 0; b--) {
		u32 bit;

		if (wait_rise(d, &raw, EDGE_BUDGET))
			return -EIO;
		bit = io_dat0(raw);
		host_crc |= bit << b;
	}

	/* End bit. */
	if (wait_rise(d, &raw, EDGE_BUDGET))
		return -EIO;

	/*
	 * CRC status token on DAT0: 0-1-0-1-0 framing with the 3-bit status
	 * "010" = accepted, "101" = CRC error. Then assert programming-busy
	 * (DAT0 low) and release.
	 */
	io_dat_output(d, BIT_D0);
	{
		u32 status = (crc == host_crc) ? 0x2 : 0x5; /* 010 vs 101 */
		int tok[5];

		tok[0] = 0;				/* start */
		tok[1] = (status >> 2) & 1;
		tok[2] = (status >> 1) & 1;
		tok[3] = (status >> 0) & 1;
		tok[4] = 1;				/* end */
		for (i = 0; i < 5; i++) {
			if (wait_fall(d, &raw, EDGE_BUDGET))
				goto out;
			io_dat0_drive(d, tok[i]);
		}
		/* Busy low while "programming". */
		if (wait_fall(d, &raw, EDGE_BUDGET))
			goto out;
		io_dat0_drive(d, 0);
		for (i = 0; i < 16; i++)
			if (wait_fall(d, &raw, EDGE_BUDGET))
				break;
	}
out:
	io_dat_release(d);
	touch_softlockup_watchdog();

	return (crc == host_crc) ? 0 : -EBADMSG;
}
