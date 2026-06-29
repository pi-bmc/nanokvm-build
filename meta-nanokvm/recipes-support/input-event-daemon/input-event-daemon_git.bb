SUMMARY = "Lightweight Linux input event daemon"
HOMEPAGE = "https://github.com/gandro/input-event-daemon"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=9f46f5a4f1e8e3e3a6c08b63b9e2f4c7"

# Buildroot source: BR2_PACKAGE_INPUT_EVENT_DAEMON=y
SRC_URI = "git://github.com/gandro/input-event-daemon;branch=master;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

do_compile() {
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}"
}

do_install() {
    install -d "${D}${sbindir}"
    install -m 0755 "${B}/input-event-daemon" "${D}${sbindir}/" || true
    install -d "${D}${sysconfdir}"
    install -m 0644 "${S}/input-event-daemon.conf.example" \
        "${D}${sysconfdir}/input-event-daemon.conf" 2>/dev/null || true
}

FILES:${PN} = "${sbindir}/input-event-daemon ${sysconfdir}/input-event-daemon.conf"
