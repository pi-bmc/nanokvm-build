SUMMARY = "config.txt for the rpi U-Boot boot image"
DESCRIPTION = "Deploys the repository's u-boot/config.txt (boot firmware \
configuration for Raspberry Pi 4/5) into the boot partition."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://config.txt"

S = "${WORKDIR}"

inherit deploy allarch nopackages

do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install[noexec] = "1"

do_deploy() {
    install -D -m 0644 ${WORKDIR}/config.txt ${DEPLOYDIR}/config.txt
}
addtask deploy after do_patch before do_build

