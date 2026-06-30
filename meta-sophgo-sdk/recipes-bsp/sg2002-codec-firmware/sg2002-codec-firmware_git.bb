SUMMARY = "Sophgo SG2002 codec firmware blobs"
# Pre-built proprietary firmware blobs with no accompanying license file; CLOSED
# tells the license tooling there is no LIC_FILES_CHKSUM to verify.
LICENSE = "CLOSED"

# Buildroot source: BR2_PACKAGE_SG2002_CODEC_FIRMWARE=y
# Binary firmware blobs for the Sophgo SG2002 media codec.
# These are typically distributed as pre-built binaries alongside the middleware SDK.
SRC_URI = "git://github.com/scpcom/sophgo-middleware;branch=maix_mmf-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# These are pre-built firmware blobs shipped in the middleware tree — there is
# nothing to compile. Without this, base do_compile runs oe_runmake against the
# middleware Makefile and fails ("PROJECT_FULLNAME not defined").
do_configure[noexec] = "1"
do_compile[noexec] = "1"

FIRMWARE_INSTALL_DIR = "${nonarch_base_libdir}/firmware/sophgo"

do_install() {
    install -d "${D}${FIRMWARE_INSTALL_DIR}"
    # Codec firmware blobs are typically under middleware/v2/sophpi/hardware/
    # or a dedicated firmware/ directory in the middleware tree.
    find "${S}" \( -name "*.bin" -o -name "*.fw" \) \
        -path "*/firmware/*" \
        -exec install -m 0644 {} "${D}${FIRMWARE_INSTALL_DIR}/" \; 2>/dev/null || \
        bbwarn "No codec firmware blobs found in middleware source tree; \
ensure the maix_mmf-cvisdk branch includes them"
}

FILES:${PN} = "${nonarch_base_libdir}/firmware"

LICENSE_FLAGS = "commercial"
