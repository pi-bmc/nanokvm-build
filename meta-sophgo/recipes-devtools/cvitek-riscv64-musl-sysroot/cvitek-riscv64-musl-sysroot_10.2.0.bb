SUMMARY = "T-Head RISC-V musl sysroot installed on-device for native compilation"
DESCRIPTION = "Installs the Cvitek/T-Head riscv64-unknown-linux-musl toolchain sysroot \
(headers + libraries) into the target rootfs, allowing software to be compiled \
directly on the SG2002.  Equivalent to BR2_PACKAGE_CVITEK_RISCV64_MUSL_SYSROOT=y."
LICENSE = "GPL-2.0-with-GCC-exception & MIT"
LIC_FILES_CHKSUM = ""

# The same tarball that prepare-licheesgnano.sh / host/replace-all-thead-toolchains.sh downloads.
# Checksum from host/riscv64-gcc-thead_20230307-10.2.0-x86_64.sha256
TC_RELEASE = "riscv64-gcc-thead_20230307-10.2.0-x86_64"
TC_BASE    = "riscv64-linux-musl"
TC_TARBALL = "${TC_BASE}-gcc-thead_20230307-10.2.0-x86_64.tar.gz"

SRC_URI = "https://github.com/scpcom/riscv-gnu-toolchain/releases/download/${TC_RELEASE}/${TC_TARBALL};name=tc"
SRC_URI[tc.sha256sum] = "22c0a19968b5edba4153ed9269ed4e8b7a3dd821c0e4ae56ca77789693fb9a54"

# The tarball extracts to riscv64-unknown-linux-musl/
S = "${WORKDIR}/riscv64-unknown-linux-musl"

# If prepare-licheesgnano.sh has already run and extracted the toolchain to
# host-tools/gcc/, override SRC_URI in local.conf to skip the download:
#
#   FILESEXTRAPATHS:prepend:pn-cvitek-riscv64-musl-sysroot := "${TOPDIR}/../host-tools/gcc:"
#   SRC_URI:pn-cvitek-riscv64-musl-sysroot = "file://riscv64-unknown-linux-musl"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# We only install the sysroot portion, not the cross-compiler executables.
SYSROOT_SRC = "${S}/riscv64-unknown-linux-musl/sysroot"

do_install() {
    # Headers
    install -d "${D}${includedir}"
    cp -a "${SYSROOT_SRC}/usr/include/." "${D}${includedir}/"

    # Libraries (shared + static)
    install -d "${D}${libdir}"
    cp -a "${SYSROOT_SRC}/lib/." "${D}${libdir}/"
    cp -a "${SYSROOT_SRC}/usr/lib/." "${D}${libdir}/"
}

# Prevent QA complaints about pre-built binaries in the sysroot.
INSANE_SKIP:${PN} = "already-stripped dev-so"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"

FILES:${PN} = "${libdir}"
FILES:${PN}-dev = "${includedir} ${libdir}/*.a ${libdir}/*.la"

# This is a large package; mark it as optional.
# Add cvitek-riscv64-musl-sysroot to IMAGE_INSTALL only when on-device
# compilation is required.
