SUMMARY = "Pre-seeded U-Boot environment image for the raw MMC env sectors"
DESCRIPTION = "Builds the 64 KiB environment blob that the wic writes at \
offset 512 KiB, so a freshly flashed card has a valid, known A/B state \
(slot a) instead of falling back to the compiled-in defaults with a bad-CRC \
warning."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://uboot-default-env.txt"

DEPENDS = "u-boot-mkenvimage-native"
COMPATIBLE_MACHINE = "sg2002-licheervnano"
INHIBIT_DEFAULT_DEPS = "1"
PACKAGES = ""
do_install[noexec] = "1"
do_populate_sysroot[noexec] = "1"

inherit deploy nopackages

# Must match CONFIG_ENV_SIZE in the u-boot recipe.
UBOOT_ENV_SIZE ?= "0x10000"

do_compile() {
    # -r marks the blob as the redundant-env format (an extra flag byte after
    # the CRC). The redundant *copy* at 0x90000 is deliberately left unwritten:
    # U-Boot accepts one valid copy, and writes the second on the first
    # saveenv. Seeding both identical copies would leave two with equal flags.
    mkenvimage -r -s ${UBOOT_ENV_SIZE} -o ${B}/uboot-env.bin ${WORKDIR}/uboot-default-env.txt
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/uboot-env.bin ${DEPLOYDIR}/uboot-env.bin
}
addtask deploy after do_compile before do_build
