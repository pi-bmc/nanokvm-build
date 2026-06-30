SUMMARY = "Linux kernel for Sophgo SG200X (SG2002)"
SECTION = "kernel"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

inherit kernel

LINUX_VERSION = "5.10"
LINUX_VERSION_EXTENSION = "-sophgo"
PV = "${LINUX_VERSION}+git${SRCPV}"

SRC_URI = "git://github.com/scpcom/linux;branch=licheervnano-merged-5.10.y;protocol=https \
           "

# Pin to a specific commit for reproducible builds; AUTOREV always fetches HEAD
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# Board defconfig lives in the sophgo-build submodule; FILESEXTRAPATHS resolves it.
# To use the defconfig from the locally checked-out build/ submodule, set:
#   FILESEXTRAPATHS:prepend := "${TOPDIR}/../build/boards/sg200x/sg2002_licheervnano_sd/linux:"
# in a bbappend, or copy sg2002_licheervnano_sd_defconfig alongside this recipe.
KBUILD_DEFCONFIG = "sg2002_licheervnano_sd_defconfig"

# If the defconfig is not in the kernel tree, copy it from the FILESEXTRAPATHS path.
do_configure:prepend() {
    if [ ! -f "${S}/arch/riscv/configs/${KBUILD_DEFCONFIG}" ]; then
        install -d "${S}/arch/riscv/configs"
        cp "${WORKDIR}/${KBUILD_DEFCONFIG}" "${S}/arch/riscv/configs/" || \
            bbwarn "Defconfig ${KBUILD_DEFCONFIG} not found in kernel tree or WORKDIR"
    fi
}

# Board DTS files are symlinked into the kernel tree by the upstream build system.
# The licheervnano-merged-5.10.y branch already includes them at:
#   arch/riscv/boot/dts/cvitek/sg2002_licheervnano_sd.dts
# If you need to override the DTS, add file://sg2002_licheervnano_sd.dts to SRC_URI
# and copy it in do_configure:prepend.

# The cvitek 5.10 kernel predates GCC 13/14 default-error diagnostics; keep them
# as warnings so it builds with the oe-core toolchain.
export KCFLAGS = "-Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=incompatible-pointer-types"
