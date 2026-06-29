# External toolchain configuration for Sophgo SG2002 / T-Head C906
#
# This file wires Yocto to the pre-built T-Head RISC-V GCC (same toolchain
# used by the Buildroot build: riscv64-unknown-linux-musl, GCC 10.2.0).
#
# USAGE
# -----
# 1. Add meta-external-toolchain to your build:
#      kas build kas-external-tc.yml
#    or clone it manually and add to bblayers.conf:
#      git clone https://github.com/siemens/meta-external-toolchain yocto-layers/meta-external-toolchain
#
# 2. Ensure the T-Head toolchain is present (prepare-licheesgnano.sh downloads it):
#      ./prepare-licheesgnano.sh   # or just the toolchain download section
#
# 3. Uncomment the line in sg2002-licheervnano.conf:
#      require conf/machine/include/sg2002-external-toolchain.inc
#
# WHY USE THE VENDOR TOOLCHAIN?
# The T-Head GCC adds vendor-specific ISA extensions not in upstream GCC 13/14:
#   -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mabi=lp64d
# These are required for the Sophgo middleware (ISP, H.264 encoder) which
# calls T-Head vector/custom intrinsics at runtime.  For pure NanoKVM I/O
# (networking, USB gadget) the standard Yocto RISC-V toolchain is sufficient.

# Tell Yocto to use an external toolchain instead of building its own.
TCMODE = "external"

# Root of the T-Head GCC install.  host/replace-all-thead-toolchains.sh downloads
# and extracts the toolchain here.  The tarball is:
#   https://github.com/scpcom/riscv-gnu-toolchain/releases/download/
#     riscv64-gcc-thead_20230307-10.2.0-x86_64/
#     riscv64-linux-musl-gcc-thead_20230307-10.2.0-x86_64.tar.gz
# sha256: 22c0a19968b5edba4153ed9269ed4e8b7a3dd821c0e4ae56ca77789693fb9a54
# (from host/riscv64-gcc-thead_20230307-10.2.0-x86_64.sha256)
EXTERNAL_TOOLCHAIN = "${TOPDIR}/../host-tools/gcc"

# Toolchain binary directory (used by meta-external-toolchain).
EXTERNAL_TOOLCHAIN_RUN_DIR = "${EXTERNAL_TOOLCHAIN}/bin"

# Cross-compiler prefix (matches BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX).
EXTERNAL_TOOLCHAIN_PREFIX = "riscv64-unknown-linux-musl-"

# Sysroot inside the toolchain directory.
EXTERNAL_TOOLCHAIN_SYSROOT = "${EXTERNAL_TOOLCHAIN}/riscv64-unknown-linux-musl/sysroot"

# Suppress Yocto's own GCC/binutils/libc builds — the external toolchain
# provides all of these.
ASSUME_PROVIDED += " \
    virtual/${TARGET_PREFIX}gcc \
    virtual/${TARGET_PREFIX}gcc-initial \
    virtual/${TARGET_PREFIX}binutils \
    virtual/libc \
    virtual/libintl \
    virtual/libiconv \
    libgcc-initial \
    "

# T-Head C906-specific compiler flags (matches BR2_TARGET_LDFLAGS).
# These enable the 0.7 vector ISA and T-Head custom instructions.
TUNE_CCARGS:append = " -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d"

# Yocto sets TARGET_CFLAGS / TARGET_LDFLAGS from TUNE_CCARGS automatically.
# If the middleware recipe overrides CFLAGS directly, also add these there.
