SUMMARY = "T-Head riscv64-unknown-linux-musl cross toolchain (GCC 10.2.0)"
DESCRIPTION = "Pre-built XuanTie/T-Head RISC-V GCC used to build the cvitek FSBL, \
which uses C906 vendor CSRs (mhcr) and custom instructions (sync.i) that upstream \
GCC/binutils cannot assemble. Provided as a -native build tool only."
# Pre-built GPL toolchain used only as a host build tool (not shipped on target).
LICENSE = "CLOSED"

# Same tarball + checksum as cvitek-riscv64-musl-sysroot (verified).
TC_RELEASE = "riscv64-gcc-thead_20230307-10.2.0-x86_64"
TC_TARBALL = "riscv64-linux-musl-gcc-thead_20230307-10.2.0-x86_64.tar.gz"

SRC_URI = "https://github.com/scpcom/riscv-gnu-toolchain/releases/download/${TC_RELEASE}/${TC_TARBALL};name=tc"
SRC_URI[tc.sha256sum] = "22c0a19968b5edba4153ed9269ed4e8b7a3dd821c0e4ae56ca77789693fb9a54"

# The tarball extracts to riscv64-linux-musl-x86_64/ (contains bin/, lib/, and
# the riscv64-unknown-linux-musl/ sysroot). The cross-compiler prefix is
# riscv64-unknown-linux-musl-.
S = "${WORKDIR}/riscv64-linux-musl-x86_64"

# Provide the -native variant the FSBL build depends on.
BBCLASSEXTEND = "native"

# Install location within the native sysroot; the fsbl recipe puts ${THEAD_TC_DIR}/bin
# on PATH and uses CROSS_COMPILE=riscv64-unknown-linux-musl-.
THEAD_TC_DIR = "${datadir}/cvitek-thead-toolchain"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d "${D}${THEAD_TC_DIR}"
    cp -a "${S}/." "${D}${THEAD_TC_DIR}/"
}

# Pre-built host binaries: don't strip or QA-check them.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} = "already-stripped staticdev libdir"
