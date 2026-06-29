SUMMARY = "NanoKVM — IP KVM server for LicheeRV Nano"
HOMEPAGE = "https://github.com/sipeed/NanoKVM"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://src/github.com/sipeed/NanoKVM/LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

inherit go-mod go update-rc.d

GO_IMPORT = "github.com/sipeed/NanoKVM"
SRCREV = "${AUTOREV}"
SRC_URI = " \
    git://github.com/sipeed/NanoKVM;branch=main;protocol=https \
    file://nanokvm.init \
    "

S = "${WORKDIR}/git"

INITSCRIPT_NAME = "nanokvm"
INITSCRIPT_PARAMS = "defaults 99"

do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nanokvm.init ${D}${sysconfdir}/init.d/nanokvm

    install -d ${D}${datadir}/nanokvm
}

FILESEXTRAPATHS:prepend := "${THISDIR}/nanokvm:"

FILES:${PN} = " \
    ${bindir}/nanokvm \
    ${datadir}/nanokvm \
    ${sysconfdir}/nanokvm \
    ${sysconfdir}/init.d/nanokvm \
    "
