SUMMARY = "SPI device test tool from Linux kernel tools/"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

# Buildroot source: BR2_PACKAGE_SPIDEV_TEST=y
# Sourced from the kernel tree's tools/spi/spidev_test.c
DEPENDS = "virtual/kernel"

inherit kernel-arch

SRC_URI = "git://github.com/scpcom/linux;branch=licheervnano-merged-5.10.y;protocol=https \
           "
SRCREV = "${AUTOREV}"
PV = "5.10+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        "${S}/tools/spi/spidev_test.c" \
        -o "${B}/spidev_test"
}

do_install() {
    install -d "${D}${bindir}"
    install -m 0755 "${B}/spidev_test" "${D}${bindir}/spidev_test"
}

FILES:${PN} = "${bindir}/spidev_test"
