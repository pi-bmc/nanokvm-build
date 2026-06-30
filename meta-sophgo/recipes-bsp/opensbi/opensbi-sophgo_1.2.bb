SUMMARY = "OpenSBI RISC-V SBI firmware for Sophgo SG200X"
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://COPYING.BSD;md5=42dd9555eb177f35150cf9aa240b61e5"

inherit deploy

DEPENDS = "u-boot-sophgo"

SRC_URI = "git://github.com/scpcom/opensbi;branch=licheervnano-cvisdk-1.2;protocol=https"
SRCREV = "${AUTOREV}"
PV = "1.2+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

PLATFORM = "generic"
FW_PAYLOAD ?= "y"

# CHIP_ARCH selects the prebuilt power-management SRAM blob
# (pm_default_cv181x.bin) the cvitek platform .incbin's; OPENSBI_PATH is the
# prefix the Makefile joins it with.
EXTRA_OEMAKE = "CROSS_COMPILE=${TARGET_PREFIX} \
                PLATFORM=${PLATFORM} \
                FW_PAYLOAD=${FW_PAYLOAD} \
                CHIP_ARCH=CV181X \
                OPENSBI_PATH=${S}"

do_compile:prepend() {
    # SG2002 is in the cv181x family. The cvitek OpenSBI platform code gates
    # chip specifics (e.g. SUSPEND_SRAM_ENTRY) on CONFIG_CV181X, which the
    # sophgo-build orchestration defines from CHIP_ARCH; inject it here.
    if ! grep -q "DCONFIG_CV181X" "${S}/platform/generic/cvitek/objects.mk"; then
        # GENFLAGS (the actual compile flags) pulls in platform-genflags-y.
        # -Ulinux: the OE target triplet path (riscv64-oe-linux-musl) is passed
        # unquoted via -DPM_SRAM_BIN_PATH and .incbin'd; GCC's predefined `linux`
        # macro would otherwise rewrite "linux" -> "1" and break the path.
        echo 'platform-genflags-y += -DCONFIG_CV181X -Ulinux' >> \
            "${S}/platform/generic/cvitek/objects.mk"
    fi
}

do_compile() {
    oe_runmake -C ${S} all
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${S}/build/platform/${PLATFORM}/firmware/fw_dynamic.bin \
        ${DEPLOYDIR}/fw_dynamic.bin || true
    install -m 0644 ${S}/build/platform/${PLATFORM}/firmware/fw_dynamic.elf \
        ${DEPLOYDIR}/fw_dynamic.elf || true
}
addtask do_deploy after do_compile before do_build

PROVIDES += "virtual/opensbi"
