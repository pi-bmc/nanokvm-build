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

EXTRA_OEMAKE = "CROSS_COMPILE=${TARGET_PREFIX} \
                PLATFORM=${PLATFORM} \
                FW_PAYLOAD=${FW_PAYLOAD}"

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
