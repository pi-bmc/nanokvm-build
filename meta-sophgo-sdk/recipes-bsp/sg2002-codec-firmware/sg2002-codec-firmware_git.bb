SUMMARY = "Sophgo SG2002 codec firmware blobs"
LICENSE = "Proprietary"
LIC_FILES_CHKSUM = ""

# Buildroot source: BR2_PACKAGE_SG2002_CODEC_FIRMWARE=y
# Binary firmware blobs for the Sophgo SG2002 media codec.
# These are typically distributed as pre-built binaries alongside the middleware SDK.
SRC_URI = "git://github.com/scpcom/sophgo-middleware;branch=maix_mmf-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

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
