SUMMARY = "First-Stage Bootloader for Sophgo SG200X"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=90b4a3b2dd63ee0498a1db08a1c66859"

DEPENDS = "opensbi-sophgo"

SRC_URI = "git://github.com/scpcom/sophgo-fsbl;branch=licheervnano;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

CHIP ?= "cv181x"
BOARD ?= "licheervnano"
BOOT_CPU ?= "riscv"

EXTRA_OEMAKE = "CHIP=${CHIP} BOARD=${BOARD} BOOT_CPU=${BOOT_CPU} \
                OUTPUT=${B}/fsbl_output"

do_compile() {
    oe_runmake -C ${S}
}

do_install() {
    install -d ${D}${datadir}/fsbl
    install -m 0644 ${S}/build/cv181x_licheervnano/fip.bin ${D}${datadir}/fsbl/ || true
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${S}/build/cv181x_licheervnano/fip.bin ${DEPLOYDIR}/ || true
}
addtask do_deploy after do_install before do_build

FILES:${PN} = "${datadir}/fsbl"
