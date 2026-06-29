SUMMARY = "Lightweight SSDP/UPnP responder"
HOMEPAGE = "https://github.com/troglobit/ssdp-responder"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://LICENSE;md5=f0a90f7b8bd43f0c5b70b60d81d3cd58"

SRC_URI = "git://github.com/troglobit/ssdp-responder;branch=master;protocol=https"
SRCREV = "${AUTOREV}"
PV = "1.9+git${SRCPV}"

S = "${WORKDIR}/git"

inherit autotools pkgconfig update-rc.d

INITSCRIPT_NAME = "ssdpd"
INITSCRIPT_PARAMS = "defaults 90"

do_install:append() {
    if [ -f "${D}${sysconfdir}/init.d/ssdpd" ]; then
        chmod 0755 "${D}${sysconfdir}/init.d/ssdpd"
    fi
}

FILES:${PN} = "${sbindir}/ssdpd ${sysconfdir}/init.d/ssdpd"
