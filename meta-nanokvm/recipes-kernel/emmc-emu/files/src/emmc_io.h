/* SPDX-License-Identifier: GPL-2.0 */
/*
 * emmc_io.h - hot-path line accessors.
 *
 * These are the inner-loop primitives used by emmc_phy.c. Every one compiles
 * to a single relaxed MMIO access plus a shift/mask. We deliberately use the
 * *_relaxed variants: on this single-core RISC-V part the only device we touch
 * in the critical section is this one GPIO bank, ordering between our own
 * accesses is program order, and the DMB in the non-relaxed accessors would
 * roughly double the per-bit cost - unaffordable at a 2.5 us bit period.
 *
 * Direction/output are tracked in software shadows (d->ddr_shadow / d->dr_shadow)
 * so a single line can be flipped without a read-modify-write of the live
 * register (a live RMW would also be a correctness hazard: the CLK/CMD inputs
 * read back as 0 in DR and could be clobbered).
 *
 * Signal electrical model: we drive push-pull. The eMMC bus has a pull-up on
 * CMD and each DAT line, and the protocol is strictly half-duplex, so actively
 * driving a '1' during our response/data window never contends with the host.
 * When we are not the transmitter we tri-state the line (DDR=input) and let the
 * host (or the pull-up) own it.
 */
#ifndef EMMC_IO_H
#define EMMC_IO_H

#include <linux/io.h>
#include "emmc_emu.h"

/* Raw snapshot of all pads in one read. */
static __always_inline u32 io_sample(struct emmc_dev *d)
{
	return readl_relaxed(d->reg_in);
}

static __always_inline u32 io_clk(u32 raw) { return (raw >> PIN_CLK) & 1; }
static __always_inline u32 io_cmd(u32 raw) { return (raw >> PIN_CMD) & 1; }
static __always_inline u32 io_dat0(u32 raw) { return (raw >> PIN_D0) & 1; }

/* DAT[3:0] as a right-aligned nibble (D3 D2 D1 D0). */
static __always_inline u32 io_dat_nibble(u32 raw)
{
	return ((raw >> PIN_D3) & 1) << 3 |
	       ((raw >> PIN_D2) & 1) << 2 |
	       ((raw >> PIN_D1) & 1) << 1 |
	       ((raw >> PIN_D0) & 1);
}

static __always_inline void io_ddr_commit(struct emmc_dev *d)
{
	writel_relaxed(d->ddr_shadow, d->reg_ddr);
}
static __always_inline void io_dr_commit(struct emmc_dev *d)
{
	writel_relaxed(d->dr_shadow, d->reg_dr);
}

/* CMD line ------------------------------------------------------------- */

/* Drive CMD as an output with the given level (push-pull). */
static __always_inline void io_cmd_drive(struct emmc_dev *d, u32 bit)
{
	if (bit)
		d->dr_shadow |= BIT_CMD;
	else
		d->dr_shadow &= ~BIT_CMD;
	io_dr_commit(d);
}

/* Make CMD an output (call once before a run of io_cmd_drive). */
static __always_inline void io_cmd_output(struct emmc_dev *d)
{
	d->ddr_shadow |= BIT_CMD;
	io_ddr_commit(d);
}

/* Release CMD back to the host (tri-state). */
static __always_inline void io_cmd_release(struct emmc_dev *d)
{
	d->ddr_shadow &= ~BIT_CMD;
	io_ddr_commit(d);
}

/* DAT lines ------------------------------------------------------------ */

static __always_inline void io_dat_output(struct emmc_dev *d, u32 mask)
{
	d->ddr_shadow |= (mask & BIT_DAT_ALL);
	io_ddr_commit(d);
}

static __always_inline void io_dat_release(struct emmc_dev *d)
{
	d->ddr_shadow &= ~BIT_DAT_ALL;
	io_ddr_commit(d);
}

/* Drive DAT0 only (1-bit mode). */
static __always_inline void io_dat0_drive(struct emmc_dev *d, u32 bit)
{
	if (bit)
		d->dr_shadow |= BIT_D0;
	else
		d->dr_shadow &= ~BIT_D0;
	io_dr_commit(d);
}

/* Drive a right-aligned nibble onto D3..D0 (4-bit mode). */
static __always_inline void io_dat_drive_nibble(struct emmc_dev *d, u32 nib)
{
	u32 v = d->dr_shadow & ~BIT_DAT_ALL;

	if (nib & 8) v |= BIT_D3;
	if (nib & 4) v |= BIT_D2;
	if (nib & 2) v |= BIT_D1;
	if (nib & 1) v |= BIT_D0;
	d->dr_shadow = v;
	io_dr_commit(d);
}

#endif /* EMMC_IO_H */
