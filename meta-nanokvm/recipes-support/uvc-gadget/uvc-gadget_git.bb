SUMMARY = "UVC gadget userspace daemon"
HOMEPAGE = "https://github.com/wlhe/uvc-gadget"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263"

# Buildroot source: BR2_PACKAGE_UVC_GADGET=y
SRC_URI = "git://github.com/wlhe/uvc-gadget;branch=master;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.2+git${SRCPV}"

S = "${WORKDIR}/git"

DEPENDS = "virtual/kernel"

# Fold OE LDFLAGS into the compiler so the Makefile's `$(CC) ... -o` link emits
# --hash-style=gnu even though it ignores $(LDFLAGS) for the object/link rules.
TARGET_CC_ARCH += "${LDFLAGS}"

do_compile() {
    # The Makefile links with `$(CC) $(LDFLAGS)` but defaults LDFLAGS to "-g",
    # dropping OE's --hash-style=gnu (the do_package_qa GNU_HASH check fails).
    # Clean first so a stale (pre-fix) binary is not left un-relinked, then pass
    # OE CC (which now carries LDFLAGS via TARGET_CC_ARCH) and LDFLAGS through.
    oe_runmake clean || true
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"
}

do_install() {
    install -d "${D}${bindir}"
    install -m 0755 "${B}/uvc-gadget" "${D}${bindir}/" || true
}

FILES:${PN} = "${bindir}/uvc-gadget"
