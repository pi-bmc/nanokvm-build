SUMMARY = "U-Boot for Sophgo SG200X (SG2002) — LicheeRV Nano"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://Licenses/README;md5=5a7450c57ffe5ae63fd732446b988025"

require recipes-bsp/u-boot/u-boot.inc

DEPENDS += "bc-native dtc-native"

SRC_URI = "git://github.com/scpcom/u-boot;branch=licheervnano-cvisdk-2021.10;protocol=https"

SRCREV = "${AUTOREV}"
PV = "2021.10+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

UBOOT_MACHINE = "sg2002_licheervnano_sd_defconfig"

# The licheervnano-cvisdk-2021.10 branch includes board/cvitek/ already.
# Board-specific init files from build/boards/.../u-boot/ may override defaults;
# use FILESEXTRAPATHS in a bbappend to reference the build/ submodule:
#   FILESEXTRAPATHS:prepend := "${TOPDIR}/../build/boards/sg200x/sg2002_licheervnano_sd/u-boot:"
#   SRC_URI += "file://cvi_board_init.c file://cvitek.h"
# and copy them in do_configure:prepend.

UBOOT_ENV_SUFFIX = "txt"
UBOOT_INITIAL_ENV = ""

# U-Boot produces fip.bin (containing OpenSBI + U-Boot proper).
# Deployed alongside boot.sd for the FAT boot partition.
do_deploy:append() {
    install -m 0644 "${B}/fip.bin" "${DEPLOYDIR}/fip.bin" || true
}
