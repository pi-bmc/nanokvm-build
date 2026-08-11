/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vendored from the vendor 5.10 tree, drivers/pinctrl/cvitek/pinctrl-cv181x.h.
 *
 * Trimmed: the original pulls in drivers/pinctrl/core.h, a subsystem-private
 * header that is not available to external modules. cif.c references no
 * pinctrl_dev/pinctrl_desc type, only the PINMUX_* macros and the pad/function
 * constants, so dropping it costs nothing.
 *
 * NOTE, and this is a real conflict rather than a rename: PINMUX_CONFIG() pokes
 * the pinmux block at 0x03001000 directly through its own ioremap, while
 * mainline's pinctrl-sg2002 owns that same window (sg2002.dtsi "pinctrl" node,
 * reg = <0x03001000 0x1000>). Nothing enforces exclusion -- ioremap() does not
 * take the region -- so these writes land behind pinctrl's back and its state
 * tracking will not reflect them.
 *
 * It is left as-is for now to keep the vendor driver behaviourally identical.
 * The correct fix is to describe the CSI/TTL pins as a pinctrl state in the
 * board DTS and drop these pokes; that is worth doing before this driver is
 * trusted on hardware, because pinctrl and cif can otherwise fight over the
 * same register.
 */


#ifndef __PINCTRL_CV181X_H__
#define __PINCTRL_CV181X_H__

#include "cv181x_pinlist_swconfig.h"
#include "cv181x_reg_fmux_gpio.h"

#define PINMUX_BASE 0x03001000
#define PINMUX_RANGE 0xC8C + 4 // the last pinctl reg of 181x
#define PINMUX_MASK(PIN_NAME) FMUX_GPIO_FUNCSEL_##PIN_NAME##_MASK
#define PINMUX_OFFSET(PIN_NAME) FMUX_GPIO_FUNCSEL_##PIN_NAME##_OFFSET
#define PINMUX_VALUE(PIN_NAME, FUNC_NAME) PIN_NAME##__##FUNC_NAME
#define PINMUX_CONFIG(PIN_NAME, FUNC_NAME) \
		mmio_clrsetbits_32(PINMUX_BASE + FMUX_GPIO_FUNCSEL_##PIN_NAME, \
						   PINMUX_MASK(PIN_NAME) << PINMUX_OFFSET(PIN_NAME), \
						   PINMUX_VALUE(PIN_NAME, FUNC_NAME))

static inline void mmio_clrsetbits_32(uintptr_t addr,
				      uint32_t clear,
				      uint32_t set)
{
	void __iomem *tpreg;

	tpreg = ioremap(addr, 0x4);
	if (IS_ERR(tpreg)) {
		pr_err("ioremap %p failed\n", (void *)addr);
		return;
	}

	iowrite32((ioread32(tpreg) & ~clear) | set, tpreg);

	iounmap(tpreg);
}

#endif /* __PINCTRL_CV181X_H__ */
