SUMMARY = "ipmi_sim init script for NanoKVM"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "openipmi"
RDEPENDS:${PN} = "openipmi"

SRC_URI = "file://S99ipmi_sim"

S = "${WORKDIR}"

# Source the overlay file from the nanokvm/ directory in this repo.
FILESEXTRAPATHS:prepend := "${TOPDIR}/../nanokvm/overlay/etc/init.d:"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/S99ipmi_sim ${D}${sysconfdir}/init.d/S99ipmi_sim
}

FILES:${PN} = "${sysconfdir}/init.d/S99ipmi_sim"
