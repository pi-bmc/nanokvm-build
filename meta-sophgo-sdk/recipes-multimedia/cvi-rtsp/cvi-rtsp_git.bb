SUMMARY = "Sophgo CVI RTSP server library for SG200X"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=6b0ba0a65f88bb3b470ef5b9fb5c35e1"

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
