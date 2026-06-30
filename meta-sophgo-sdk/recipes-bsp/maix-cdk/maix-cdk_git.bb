SUMMARY = "MaixCDK — Sophgo Maix C/C++ development kit for SG200X"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=6b0a031c0458b51a1c975baf059fc6cf"

# Buildroot source: BR2_PACKAGE_MAIX_CDK=y
# The MaixCDK provides runtime libraries and utilities used by NanoKVM and MaixPy.
inherit cmake

DEPENDS = "sophgo-middleware"

SRC_URI = "git://github.com/sipeed/MaixCDK;branch=main;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

EXTRA_OECMAKE = " \
    -DCROSS_COMPILE=${TARGET_PREFIX} \
    -DTARGET_ARCH=riscv64 \
    -DCHIP=sg2002 \
    "

do_install() {
    install -d ${D}${libdir}
    install -d ${D}${includedir}/maix
    find ${S} -name "*.so*" -exec install -m 0755 {} ${D}${libdir}/ \; 2>/dev/null || true
    find ${S} -name "*.h" -path "*/include/*" \
        -exec install -m 0644 {} ${D}${includedir}/maix/ \; 2>/dev/null || true
}

FILES:${PN} = "${libdir} ${bindir}"
FILES:${PN}-dev = "${includedir}"

INSANE_SKIP:${PN} = "ldflags already-stripped"
