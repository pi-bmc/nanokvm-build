SUMMARY = "Sophgo CVI RTSP server library for SG200X"
LICENSE = "CLOSED"

inherit cmake

DEPENDS = "sophgo-middleware"

SRC_URI = "git://github.com/scpcom/cvi_rtsp;branch=licheervnano-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

EXTRA_OECMAKE = "-DCROSS_COMPILE_PREFIX=${TARGET_PREFIX}"

FILES:${PN} = "${libdir} ${bindir}"
FILES:${PN}-dev = "${includedir}"
