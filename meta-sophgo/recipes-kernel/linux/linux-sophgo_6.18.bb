SUMMARY = "Mainline Linux kernel 6.18 for Sophgo SG2002 (LicheeRV Nano)"
SECTION = "kernel"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

inherit kernel

LINUX_VERSION = "6.18.38"
LINUX_VERSION_EXTENSION = "-sophgo"
PV = "${LINUX_VERSION}+git${SRCPV}"

# Mainline upstream. SG2002 / LicheeRV Nano B support landed upstream (DT by
# Bootlin: sophgo,sg2002-* pinctrl/clk, dw-apb UART/I2C/SPI/GPIO, dwcmshc
# SD/eMMC, cv1800b GMAC, saradc, axi-dma, rtc). This replaces the vendor
# scpcom 5.10 BSP; the closed multimedia/AI stack (VI/VPSS/VENC/ISP/TPU) is not
# present in mainline and is intentionally dropped (see nanokvm-image.bb).
SRC_URI = "git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git;branch=linux-6.18.y;protocol=https"

# v6.18.38 (stable point release). Pinned so the DTS backports apply
# deterministically; all three (0001/0002/0003) re-verified to apply at this tag.
SRCREV = "e46dc0adfe39724bcf52cea47b8f9c9aed86a394"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# Mainline uses the unified riscv defconfig (CONFIG_ARCH_SOPHGO=y). Board- and
# NanoKVM-specific symbols are merged as a fragment by the meta-nanokvm bbappend.
KBUILD_DEFCONFIG = "defconfig"
