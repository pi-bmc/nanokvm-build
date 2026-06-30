SUMMARY = "ipmi_sim (OpenIPMI lanserv) init script and config for NanoKVM BMC"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# ipmi_sim ships with OpenIPMI (lanserv).
DEPENDS = "openipmi"
RDEPENDS:${PN} = "openipmi"

# Config and init script are vendored in files/ so the build is self-contained
# (the original recipe sourced these from the buildroot nanokvm/overlay tree).
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://S99ipmi_sim \
    file://lan.conf \
    file://ipmisim.emu \
    "

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/S99ipmi_sim ${D}${sysconfdir}/init.d/S99ipmi_sim

    install -d ${D}${sysconfdir}/ipmi
    install -m 0644 ${WORKDIR}/lan.conf ${D}${sysconfdir}/ipmi/lan.conf
    install -m 0644 ${WORKDIR}/ipmisim.emu ${D}${sysconfdir}/ipmi/ipmisim.emu
}

FILES:${PN} = " \
    ${sysconfdir}/init.d/S99ipmi_sim \
    ${sysconfdir}/ipmi/lan.conf \
    ${sysconfdir}/ipmi/ipmisim.emu \
    "

# Init script is the BusyBox/sysvinit-style S99 used by the NanoKVM rootfs.
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
