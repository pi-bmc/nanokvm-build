// SPDX-License-Identifier: GPL-2.0
/*
 * emmc_gpio.c - pad ownership and MMIO setup for the eMMC emulator.
 *
 * Responsibilities:
 *   - ioremap the PWR GPIO bank and the IOMUX block.
 *   - "claim" the six SDIO1 pads by switching their pinmux funcsel from the
 *     SDIO1 controller (0) to PWR_GPIO (3), saving the prior values so we can
 *     hand them back on unload.
 *   - set the initial directions: CLK + CMD + DAT as inputs (the host owns
 *     them at reset; we only ever drive CMD/DAT inside a response/data window).
 *
 * Pre-condition: the wifisd@4320000 SDIO1 *host* controller is disabled in the
 * device tree (status="disabled", done automatically by the linux-sophgo
 * bbappend) so the cvitek sdhci driver never probes it and never fights us for
 * these pads. See README.md / emmc-emu.dts.
 */
#include <linux/io.h>
#include <linux/delay.h>
#include "emmc_emu.h"
#include "emmc_io.h"

/* (pinmux offset, original-value slot) for each claimed pad. */
static const u16 sd1_pinmux_off[6] = {
	PINMUX_SD1_CLK, PINMUX_SD1_CMD,
	PINMUX_SD1_D0,  PINMUX_SD1_D1,
	PINMUX_SD1_D2,  PINMUX_SD1_D3,
};

int emmc_gpio_map(struct emmc_dev *d)
{
	d->gpio = ioremap(EMMC_PWR_GPIO_BASE, EMMC_PWR_GPIO_SIZE);
	if (!d->gpio)
		return -ENOMEM;

	d->pinmux = ioremap(EMMC_PINMUX_BASE, EMMC_PINMUX_SIZE);
	if (!d->pinmux) {
		iounmap(d->gpio);
		d->gpio = NULL;
		return -ENOMEM;
	}

	d->reg_dr  = d->gpio + DWGPIO_SWPORTA_DR;
	d->reg_ddr = d->gpio + DWGPIO_SWPORTA_DDR;
	d->reg_in  = d->gpio + DWGPIO_EXT_PORTA;

	return 0;
}

void emmc_gpio_unmap(struct emmc_dev *d)
{
	if (d->pinmux) {
		iounmap(d->pinmux);
		d->pinmux = NULL;
	}
	if (d->gpio) {
		iounmap(d->gpio);
		d->gpio = NULL;
	}
}

int emmc_gpio_claim_pads(struct emmc_dev *d)
{
	int i;

	/* Snapshot current direction/output so our shadows match silicon. */
	d->ddr_shadow = readl_relaxed(d->reg_ddr);
	d->dr_shadow  = readl_relaxed(d->reg_dr);

	/* All six lines start as inputs (host-driven). */
	d->ddr_shadow &= ~BIT_ALL;
	io_ddr_commit(d);

	/* Re-point each pad's funcsel from SDIO1 (0) to PWR_GPIO (3). */
	for (i = 0; i < 6; i++) {
		void __iomem *r = d->pinmux + sd1_pinmux_off[i];
		u32 v = readl_relaxed(r);

		d->pinmux_saved[i] = v;
		v = (v & ~PINMUX_FUNC_MASK) | PINMUX_FUNC_GPIO;
		writel_relaxed(v, r);
	}
	/* Make the funcsel writes visible before anyone samples the bus. */
	wmb();

	d->pads_claimed = true;
	dev_info(d->dev,
		 "claimed SDIO1 pads as PWR_GPIO (CLK=gpio%d CMD=gpio%d D0=gpio%d)\n",
		 PIN_CLK, PIN_CMD, PIN_D0);
	return 0;
}

void emmc_gpio_release_pads(struct emmc_dev *d)
{
	int i;

	if (!d->pads_claimed)
		return;

	/* Tri-state everything first. */
	d->ddr_shadow &= ~BIT_ALL;
	io_ddr_commit(d);

	/* Restore the original funcsel (back to the SDIO1 controller). */
	for (i = 0; i < 6; i++)
		writel_relaxed(d->pinmux_saved[i], d->pinmux + sd1_pinmux_off[i]);
	wmb();

	d->pads_claimed = false;
	dev_info(d->dev, "released SDIO1 pads back to SDIO1 funcsel\n");
}

void emmc_gpio_idle(struct emmc_dev *d)
{
	/* Drop everything to input; the host's pull-ups hold CMD/DAT high. */
	d->ddr_shadow &= ~BIT_ALL;
	io_ddr_commit(d);
}
