SUMMARY = "Sophgo SG200X pinmux configuration utility"
LICENSE = "CLOSED"

# Buildroot source: BR2_PACKAGE_CVI_PINMUX=y, BR2_PACKAGE_CVI_PINMUX_SG200X=y
# Fetched as part of sophgo-osdrv extras or a dedicated package in the vendor SDK.
SRC_URI = "git://github.com/scpcom/sophgo-osdrv;branch=licheervnano-cvisdk;protocol=https \
           "
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

SG200X_PINMUX_SUBDIR = "pinctrl/sg200x"

do_compile() {
    if [ -d "${S}/${SG200X_PINMUX_SUBDIR}" ]; then
        oe_runmake -C "${S}/${SG200X_PINMUX_SUBDIR}" \
            CROSS_COMPILE="${TARGET_PREFIX}" \
            CHIP=sg200x
    else
        bbwarn "cvi-pinmux source subdir ${SG200X_PINMUX_SUBDIR} not found; \
check if pinmux tool is in a different location in the osdrv tree"
    fi
}

do_install() {
    install -d "${D}${bindir}"
    find "${S}" -name "pinmux_info" -o -name "cvi_pinmux" | \
        xargs -I{} install -m 0755 {} "${D}${bindir}/" 2>/dev/null || true
}

FILES:${PN} = "${bindir}"
