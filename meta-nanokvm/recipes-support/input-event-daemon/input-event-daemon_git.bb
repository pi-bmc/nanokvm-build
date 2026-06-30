SUMMARY = "Lightweight Linux input event daemon"
HOMEPAGE = "https://github.com/gandro/input-event-daemon"
LICENSE = "CLOSED"

# Buildroot source: BR2_PACKAGE_INPUT_EVENT_DAEMON=y
SRC_URI = "git://github.com/gandro/input-event-daemon;branch=master;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

# Build only the daemon binary. The Makefile's default 'all' target also builds
# man/html docs via a2x/asciidoc (not in the build sysroot), which is not needed.
do_compile() {
    oe_runmake input-event-daemon CC="${CC}" CFLAGS="${CFLAGS}"
}

do_install() {
    install -d "${D}${sbindir}"
    install -m 0755 "${B}/input-event-daemon" "${D}${sbindir}/"
    install -d "${D}${sysconfdir}"
    # Upstream ships the sample config at docs/sample.conf.
    if [ -f "${S}/docs/sample.conf" ]; then
        install -m 0644 "${S}/docs/sample.conf" \
            "${D}${sysconfdir}/input-event-daemon.conf"
    fi
}

FILES:${PN} = "${sbindir}/input-event-daemon ${sysconfdir}/input-event-daemon.conf"
