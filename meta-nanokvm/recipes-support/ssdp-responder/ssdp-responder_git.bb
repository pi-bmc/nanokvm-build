SUMMARY = "Lightweight SSDP/UPnP responder"
HOMEPAGE = "https://github.com/troglobit/ssdp-responder"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://LICENSE;md5=dcc2c197d15d92d758ad9ce88d3016cc"

SRC_URI = "git://github.com/troglobit/ssdp-responder;branch=master;protocol=https \
           file://ssdpd"
SRCREV = "${AUTOREV}"
PV = "1.9+git${SRCPV}"

S = "${WORKDIR}/git"

inherit autotools pkgconfig update-rc.d

INITSCRIPT_NAME = "ssdpd"
INITSCRIPT_PARAMS = "defaults 90"

# Upstream ships only a systemd unit; provide the sysvinit script update-rc.d
# (and the do_rootfs postinst) expects on this sysvinit distro.
do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/ssdpd ${D}${sysconfdir}/init.d/ssdpd
}

FILES:${PN} = "${sbindir}/ssdpd ${bindir}/ssdp-scan ${sysconfdir}/init.d/ssdpd"
