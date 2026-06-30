SUMMARY = "NanoKVM software eMMC device (card) emulator over SDIO1 GPIO"
DESCRIPTION = "Out-of-tree kernel module that re-muxes the SG2002 SDIO1 pads as \
GPIO and bit-bangs the eMMC card side of the bus, so an external host (a \
Raspberry Pi running U-Boot) enumerates the NanoKVM as an eMMC device. Replaces \
the SDIO1 WiFi function. See README.md for the design and the single-core \
timing caveats."
HOMEPAGE = "https://github.com/sipeed/NanoKVM"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = " \
    file://src/Makefile \
    file://src/emmc_main.c \
    file://src/emmc_gpio.c \
    file://src/emmc_phy.c \
    file://src/emmc_proto.c \
    file://src/emmc_crc.c \
    file://src/emmc_crc.h \
    file://src/emmc_emu.h \
    file://src/emmc_io.h \
    file://src/emmc_uapi.h \
    file://emmc-emu.dts \
"

S = "${WORKDIR}/src"

# Only meaningful on the SG2002 board (uses CV181x register addresses).
COMPATIBLE_MACHINE = "sg2002-licheervnano"

# Auto-load at boot with a sensible default capacity. Edit the options line (or
# /etc/modprobe.d/emmc-emu.conf on target) to change size / clock spin budget.
KERNEL_MODULE_AUTOLOAD += "emmc_emu"
KERNEL_MODULE_PROBECONF += "emmc_emu"
module_conf_emmc_emu = "options emmc_emu emmc_capacity_mb=16 emmc_force_legacy=1"

RPROVIDES:${PN} += "kernel-module-emmc-emu"
