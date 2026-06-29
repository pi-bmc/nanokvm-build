SUMMARY = "AXP2101 PMIC userspace tool for LicheeRV Nano"
LICENSE = "CLOSED"

# Buildroot source: BR2_PACKAGE_AXP2101=y
# Userspace utility for the X-Powers AXP2101 power management IC on LicheeRV Nano.
SRC_URI = "git://github.com/scpcom/sophgo-osdrv;branch=licheervnano-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

do_compile() {
    if [ -d "${S}/axp2101" ]; then
        oe_runmake -C "${S}/axp2101" CROSS_COMPILE="${TARGET_PREFIX}"
    else
        bbwarn "axp2101 subdirectory not found in osdrv tree; \
check if it is a separate repo or subdir"
    fi
}

do_install() {
    install -d "${D}${bindir}"
    find "${S}" -name "axp2101" -type f -perm /111 \
        -exec install -m 0755 {} "${D}${bindir}/" \; 2>/dev/null || true
}

FILES:${PN} = "${bindir}"
