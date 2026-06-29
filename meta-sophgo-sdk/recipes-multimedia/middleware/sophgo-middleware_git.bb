SUMMARY = "Sophgo multimedia middleware (ISP, sensor, encoding) for SG200X"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=6b0ba0a65f88bb3b470ef5b9fb5c35e1"

inherit cmake pkgconfig

DEPENDS = "virtual/kernel osdrv"

SRC_URI = "git://github.com/scpcom/sophgo-middleware;branch=maix_mmf-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

CHIP_ARCH = "CV181X"
BOARD_NAME = "cv181xc_wevb_0007a_emmc"

EXTRA_OECMAKE = "-DCHIP=${CHIP_ARCH} \
                 -DBOARD=${BOARD_NAME} \
                 -DCROSS_COMPILE_PREFIX=${TARGET_PREFIX}"

EXTRA_OEMAKE = "CHIP=${CHIP_ARCH} \
                KERNEL_DIR=${STAGING_KERNEL_DIR} \
                CROSS_COMPILE=${TARGET_PREFIX} \
                PREFIX=${D}${prefix}"

do_compile() {
    oe_runmake -C ${S} middleware
}

do_install() {
    oe_runmake -C ${S} install PREFIX=${D}${prefix}
    install -d ${D}${libdir}
    find ${S} -name "*.so*" -exec install -m 0755 {} ${D}${libdir}/ \; || true
    install -d ${D}${includedir}/sophgo
    find ${S} -name "*.h" -path "*/include/*" -exec \
        install -m 0644 {} ${D}${includedir}/sophgo/ \; || true
}

FILES:${PN} = "${libdir} ${bindir}"
FILES:${PN}-dev = "${includedir}"

INSANE_SKIP:${PN} = "ldflags already-stripped"
