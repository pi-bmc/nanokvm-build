SUMMARY = "TF-A BL31 armstubs for Raspberry Pi 4/5"
DESCRIPTION = "Plain TF-A BL31 (EL3 monitor + PSCI) built for PLAT=rpi4 and \
PLAT=rpi5, deployed as armstub8.bin / armstub8-2712.bin. UEFI variables live \
in U-Boot's file store, so there is no OP-TEE / StandaloneMM secure world. \
BL31 still provides PSCI, which U-Boot uses for reset, poweroff and the \
power button on bcm2712."
HOMEPAGE = "https://www.trustedfirmware.org"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://docs/license.rst;md5=6ed7bace7b0bc63021c6eba7b524039e"

# Tag (or full sha) of TF-A to build; overridden via the kas env passthrough.
TFA_REF ?= "v2.15.0"
PV = "${@d.getVar('TFA_REF').lstrip('v')}"

SRC_URI = "https://github.com/ARM-software/arm-trusted-firmware/archive/${TFA_REF}.tar.gz;downloadfilename=arm-trusted-firmware-${TFA_REF}.tar.gz;subdir=tfa-src;striplevel=1"
BB_STRICT_CHECKSUM = "ignore"

S = "${WORKDIR}/tfa-src"
B = "${S}"

DEPENDS = "dtc-native openssl-native python3-pyelftools-native"

inherit deploy python3native nopackages

COMPATIBLE_MACHINE = "pi-bmc-rpi64"

do_configure[noexec] = "1"

do_compile() {
    # TF-A's build system owns all compiler/linker flags
    unset LDFLAGS CFLAGS CPPFLAGS AS LD

    # Raspberry Pi 5 (BCM2712) armstub
    oe_runmake -C ${S} PLAT=rpi5 RPI3_RUNTIME_UART=1 DEBUG=1 \
        CROSS_COMPILE=${TARGET_PREFIX} bl31

    # Raspberry Pi 4 (BCM2711) armstub
    oe_runmake -C ${S} PLAT=rpi4 DEBUG=1 \
        CROSS_COMPILE=${TARGET_PREFIX} bl31
}

do_install[noexec] = "1"

do_deploy() {
    install -D -m 0644 ${B}/build/rpi5/debug/bl31.bin ${DEPLOYDIR}/armstub8-2712.bin
    install -D -m 0644 ${B}/build/rpi4/debug/bl31.bin ${DEPLOYDIR}/armstub8.bin
}
addtask deploy after do_compile before do_build

