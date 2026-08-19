SUMMARY = "NanoKVM boot script (boot.scr) for U-Boot's RAUC bootmeth"
DESCRIPTION = "The final boot instructions bootmeth_rauc requires the distro \
to supply on the slot's boot partition. It turns the slot the bootmeth chose \
into a load of that slot's FIT, and sets the kernel command line."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://boot.cmd"

DEPENDS = "u-boot-mkimage-native"
COMPATIBLE_MACHINE = "sg2002-licheervnano"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INHIBIT_DEFAULT_DEPS = "1"
PACKAGES = ""
do_install[noexec] = "1"
do_populate_sysroot[noexec] = "1"

inherit deploy nopackages

do_compile() {
    # A legacy uImage wrapper, which is what cmd_source_script() expects and
    # CRC-checks before running. bootmeth_rauc looks for exactly "boot.scr" or
    # "boot.scr.uimg" under "/" and "/boot" on the boot partition.
    uboot-mkimage -A riscv -O linux -T script -C none \
        -n "NanoKVM boot script" -d ${WORKDIR}/boot.cmd ${B}/boot.scr

    # bootmeth_rauc reads the script into a 64 KiB buffer (bootmeth_alloc_file
    # in distro_rauc_load_boot_script), so a script larger than that is loaded
    # truncated rather than rejected.
    sz=$(stat -c %s ${B}/boot.scr)
    if [ "$sz" -gt 65536 ]; then
        bbfatal "boot.scr is $sz B, over bootmeth_rauc's 64 KiB read buffer"
    fi
    bbplain "boot.scr: $sz B"
}

# One script serves both slots: they share p1, and the script picks its FIT
# from distro_rootpart.
do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}
addtask deploy after do_compile before do_build
