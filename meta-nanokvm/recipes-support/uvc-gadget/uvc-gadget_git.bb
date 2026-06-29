SUMMARY = "UVC gadget userspace daemon"
HOMEPAGE = "https://github.com/wlhe/uvc-gadget"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"

# Buildroot source: BR2_PACKAGE_UVC_GADGET=y
SRC_URI = "git://github.com/wlhe/uvc-gadget;branch=master;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.2+git${SRCPV}"

S = "${WORKDIR}/git"

DEPENDS = "virtual/kernel"

do_compile() {
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}"
}

do_install() {
    install -d "${D}${bindir}"
    install -m 0755 "${B}/uvc-gadget" "${D}${bindir}/" || true
}

FILES:${PN} = "${bindir}/uvc-gadget"
