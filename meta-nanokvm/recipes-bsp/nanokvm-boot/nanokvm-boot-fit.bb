SUMMARY = "NanoKVM boot payload: single FIT (kernel + FDT + initramfs)"
DESCRIPTION = "Packs the kernel Image, the board DTB and the initramfs into \
one hash-verified FIT, deployed as boot_a.itb and boot_b.itb for the A/B \
slots. Replaces extlinux's four separate file reads with one fatload, which \
matters on a board whose U-Boot SD reads are marginal."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://boot.its.in"

# The three payloads come from other recipes' deploy dirs, so this must build
# after all of them. do_deploy of each is what publishes the artifacts.
# dtc-native: mkimage shells out to dtc(1) to compile the .its into the FIT.
DEPENDS = "u-boot-mkimage-native dtc-native virtual/kernel nanokvm-initramfs-image"
do_compile[depends] += "virtual/kernel:do_deploy nanokvm-initramfs-image:do_image_complete"

COMPATIBLE_MACHINE = "sg2002-licheervnano"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INHIBIT_DEFAULT_DEPS = "1"
PACKAGES = ""

# Nothing lands in the rootfs; the FIT is a boot-partition artifact only.
do_install[noexec] = "1"
do_populate_sysroot[noexec] = "1"

inherit deploy nopackages

KERNEL_FIT_DTB ?= "sophgo/sg2002-licheerv-nano-b.dtb"
INITRAMFS_ARTIFACT ?= "nanokvm-initramfs-image-${MACHINE}.cpio.gz"

do_compile() {
    kernel="${DEPLOY_DIR_IMAGE}/Image-${MACHINE}.bin"
    dtb="${DEPLOY_DIR_IMAGE}/$(basename ${KERNEL_FIT_DTB})"
    initramfs="${DEPLOY_DIR_IMAGE}/${INITRAMFS_ARTIFACT}"

    for f in "$kernel" "$dtb" "$initramfs"; do
        [ -f "$f" ] || bbfatal "nanokvm-boot-fit: missing FIT input $f"
    done

    sed -e "s|@KERNEL@|$kernel|" \
        -e "s|@DTB@|$dtb|" \
        -e "s|@INITRAMFS@|$initramfs|" \
        ${WORKDIR}/boot.its.in > ${B}/boot.its

    uboot-mkimage -f ${B}/boot.its ${B}/boot.itb

    # Fail loudly rather than shipping a FIT that cannot be staged: the whole
    # image is loaded at kernel_addr_r=0x83000000 and the kernel is extracted
    # down to 0x80200000, so the staged FIT must not reach ramdisk_addr_r
    # (0x84000000) -- 16 MiB of staging room.
    fitsz=$(stat -c %s ${B}/boot.itb)
    budget=16777216
    bbplain "boot.itb: $(expr $fitsz / 1024) KiB (staging budget $(expr $budget / 1024) KiB)"
    if [ "$fitsz" -gt "$budget" ]; then
        bbfatal "boot.itb is $fitsz B, over the $budget B staging budget:" \
                "staged at 0x83000000 it would run into ramdisk_addr_r=0x84000000." \
                "Trim the kernel or initramfs, or re-plan the addresses in" \
                "u-boot patch 0009 and this recipe together."
    fi
}

# Both slots ship the same payload at factory. An update writes the new FIT to
# the inactive slot and flips bootslot; U-Boot's bootcount fallback swaps back
# if the new one cannot reach a healthy userspace.
do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/boot.itb ${DEPLOYDIR}/boot_a.itb
    install -m 0644 ${B}/boot.itb ${DEPLOYDIR}/boot_b.itb
}
addtask deploy after do_compile before do_build
