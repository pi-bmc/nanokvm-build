SUMMARY = "First-Stage Bootloader (FSBL) + FIP packaging for Sophgo SG2002"
LICENSE = "CLOSED"

# The FIP bundles the FSBL (BL2), OpenSBI (MONITOR/fw_dynamic) and U-Boot
# (LOADER_2ND / BL33), so both must be built and staged first.
# cvitek-thead-toolchain-native provides the T-Head GCC needed to assemble the
# C906 vendor CSRs/instructions in the FSBL BL2.
DEPENDS = "opensbi-sophgo u-boot-sophgo python3-native cvitek-thead-toolchain-native"

THEAD_TC_BIN = "${STAGING_DATADIR_NATIVE}/cvitek-thead-toolchain/bin"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "git://github.com/scpcom/sophgo-fsbl;branch=licheervnano;protocol=https \
           file://cvi_board_memmap.h"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

inherit deploy

# cvitek FSBL build parameters for the SG2002 (cv181x ASIC family).
FSBL_CHIP_ARCH ?= "cv181x"
FSBL_BOARD ?= "licheervnano"
FSBL_BOOT_CPU ?= "riscv"
# DDR3 1866 MHz x16 (256 MB) — matches CONFIG_DDR_CFG_ddr3_1866_x16 for this
# board. Without it the DDR sources (incl. ddr_pkg_info.c -> get_pkg) are not
# compiled and BL2 fails to link.
FSBL_DDR_CFG ?= "ddr3_1866_x16"

# Use the T-Head vendor cross-compiler (not the OE rv64gc one) for the FSBL.
EXTRA_OEMAKE = "CROSS_COMPILE=riscv64-unknown-linux-musl- \
                CHIP_ARCH=${FSBL_CHIP_ARCH} \
                BOARD=${FSBL_BOARD} \
                BOOT_CPU=${FSBL_BOOT_CPU} \
                DDR_CFG=${FSBL_DDR_CFG}"

# The FSBL (ATF-style) invokes `ld` directly and inherits LDFLAGS from the
# environment; OE's gcc-driver LDFLAGS (-Wl,-O1, ...) are rejected by ld. The
# FSBL provides its own link flags, so clear the OE ones. Likewise the OE
# CFLAGS/CPPFLAGS/ASFLAGS target the rv64gc toolchain, not the vendor one.
LDFLAGS = ""
CFLAGS = ""
CPPFLAGS = ""
ASFLAGS = ""
TARGET_CC_ARCH = ""

# Need the deployed OpenSBI + U-Boot binaries before packing the FIP.
do_compile[depends] += "opensbi-sophgo:do_deploy u-boot-sophgo:do_deploy"

do_compile() {
    # Build with the T-Head vendor GCC (C906 custom CSRs/instructions).
    export PATH="${THEAD_TC_BIN}:${PATH}"

    # The cvitek FSBL plat headers (plat/cv181x/include/mmap.h) include the
    # generated board memory map; place it where they can find it.
    install -m 0644 "${WORKDIR}/cvi_board_memmap.h" \
        "${S}/plat/${FSBL_CHIP_ARCH}/include/cvi_board_memmap.h"

    # fip.mk looks for OpenSBI's fw_dynamic.bin at ../opensbi/... relative to
    # the FSBL source dir (S = ${WORKDIR}/git, so ../opensbi = ${WORKDIR}/opensbi).
    install -d "${WORKDIR}/opensbi/build/platform/generic/firmware"
    install -m 0644 "${DEPLOY_DIR_IMAGE}/fw_dynamic.bin" \
        "${WORKDIR}/opensbi/build/platform/generic/firmware/fw_dynamic.bin"

    # Build the FSBL (BL2) and pack the FIP with U-Boot as the 2nd-stage loader.
    oe_runmake -C ${S} all LOADER_2ND_PATH="${DEPLOY_DIR_IMAGE}/u-boot.bin"
}

do_install() {
    install -d ${D}${datadir}/fsbl
    fip="$(find ${S}/build -name fip.bin 2>/dev/null | head -1)"
    if [ -z "${fip}" ]; then
        bbfatal "fsbl: fip.bin was not produced by the FSBL build"
    fi
    install -m 0644 "${fip}" ${D}${datadir}/fsbl/fip.bin
}

do_deploy() {
    install -d ${DEPLOYDIR}
    fip="$(find ${S}/build -name fip.bin 2>/dev/null | head -1)"
    install -m 0644 "${fip}" ${DEPLOYDIR}/fip.bin
}
addtask deploy after do_install before do_build

FILES:${PN} = "${datadir}/fsbl"
