SUMMARY = "Userspace backing-store daemon for the NanoKVM eMMC emulator"
DESCRIPTION = "Loads a disk image into /dev/emmc-emu0 and periodically syncs the \
emulated card's contents back to that file, so writes the host (Raspberry Pi) \
makes - e.g. its U-Boot EFI variable store - persist. Also prints live bus \
statistics from the kernel module."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = " \
    file://daemon/emmc-emud.c \
    file://src/emmc_uapi.h \
"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -I${WORKDIR}/src \
        ${WORKDIR}/daemon/emmc-emud.c -o ${B}/emmc-emud
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/emmc-emud ${D}${bindir}/emmc-emud
}
