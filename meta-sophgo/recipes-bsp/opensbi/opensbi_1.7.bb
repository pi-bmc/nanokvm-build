SUMMARY = "RISC-V Open Source Supervisor Binary Interface (OpenSBI)"
DESCRIPTION = "Mainline OpenSBI, generic platform, built as fw_dynamic for the \
Sophgo SG2002: the vendor FSBL (BL2) loads fw_dynamic.bin and hands it the \
next-stage (U-Boot) entry via the fw_dynamic_info struct from the fip.bin \
header. Mainline U-Boot's LicheeRV Nano port requires OpenSBI newer than 1.5 \
(doc/board/sophgo/licheerv_nano.rst). Versioned above oe-core's opensbi so \
this recipe is selected."
HOMEPAGE = "https://github.com/riscv-software-src/opensbi"
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://COPYING.BSD;md5=42dd9555eb177f35150cf9aa240b61e5"

inherit deploy

SRC_URI = "git://github.com/riscv-software-src/opensbi.git;branch=master;protocol=https"
# v1.7
SRCREV = "242542438402e2b310a82b131c5cabfc2e2f027a"

S = "${WORKDIR}/git"

RISCV_SBI_PLAT = "generic"

TARGET_CC_ARCH += "${LDFLAGS}"

EXTRA_OEMAKE = "PLATFORM=${RISCV_SBI_PLAT} I=${D} FW_PIC=y CLANG_TARGET="

# Embed the board device tree into fw_dynamic. The generic OpenSBI platform is
# entirely FDT-driven (console UART, CLINT, PLIC, harts, memory). The FSBL enters
# OpenSBI with a1 = CVIMMAP_OPENSBI_FDT_ADDR (0x80080000, bl2_helper.c) but loads
# NO device tree there, and the cv181x fiptool has no device-tree slot to add one
# -- so without an embedded FDT, generic OpenSBI reads garbage and hangs before
# handing off to U-Boot (watchdog reset loop). FW_FDT_PATH makes OpenSBI use its
# own built-in copy instead of the bogus a1. U-Boot is OF_SEPARATE (carries its
# own appended DTB), so it is unaffected by what OpenSBI passes onward.
# The board DTB is the one the kernel builds+deploys (KERNEL_DEVICETREE).
EXTRA_OEMAKE:append:sg2002-licheervnano = " FW_FDT_PATH=${DEPLOY_DIR_IMAGE}/sg2002-licheerv-nano-b.dtb"
do_compile[depends] += "${@bb.utils.contains('MACHINE', 'sg2002-licheervnano', 'linux-sophgo:do_deploy', '', d)}"

do_compile() {
    oe_runmake -C ${S} all
}

do_install() {
    oe_runmake -C ${S} install
    # Only the firmware is consumed (by the fsbl recipe, via deploy); drop the
    # SDK bits to avoid packaging warnings.
    rm -rf ${D}/include ${D}/lib*
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${S}/build/platform/${RISCV_SBI_PLAT}/firmware/fw_dynamic.bin \
        ${DEPLOYDIR}/fw_dynamic.bin
    install -m 0644 ${S}/build/platform/${RISCV_SBI_PLAT}/firmware/fw_dynamic.elf \
        ${DEPLOYDIR}/fw_dynamic.elf
}
addtask deploy after do_install before do_build

FILES:${PN} = "/share/opensbi"

PROVIDES += "virtual/opensbi"

COMPATIBLE_HOST = "(riscv64|riscv32).*"
INHIBIT_PACKAGE_STRIP = "1"
