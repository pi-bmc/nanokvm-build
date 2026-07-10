# NanoKVM board customisation for the mainline linux-sophgo kernel.
#
# The base recipe (meta-sophgo/recipes-kernel/linux/linux-sophgo_6.18.bb) builds
# mainline v6.18 with the unified riscv `defconfig`. Here we:
#   * backport the upstream cv18xx USB DTS node + enable it on nano-b,
#   * merge the NanoKVM config-fragment delta (size trim, USB gadget, i2c-slave),
#   * assert the resulting Image fits U-Boot's stock kernel staging window.
#
# There is no FIT (boot.sd) any more. U-Boot's extlinux support
# (bootmeth_extlinux -> pxe_utils) loads the raw Image at ${kernel_addr_r} and
# the DTB at ${fdt_addr_r} from the separate files named by extlinux.conf's
# KERNEL / DEVICETREE keys, then boots with `booti`. wic places both on the FAT
# boot partition via IMAGE_BOOT_FILES (see the machine conf).
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# DTS backports for USB (v6.18 has the drivers but not the DT nodes). ORDER
# matters -- applied by the base do_patch in SRC_URI order:
#   0001 upstream "Add USB support for cv18xx": adds usb@4340000 (references
#        &usbphy) to cv180x.dtsi, enables &usb on nano-b. Merged post-6.18-rc1.
#   0002 companion backport: adds the sophgo,cv1800b-usb2-phy node (&usbphy,
#        child of the top syscon) that 0001 references -- absent in v6.18, so
#        without it dtc fails "Reference to non-existent node or label usbphy".
#   0003 local board tweaks on top of 0001: &usb -> dr_mode "peripheral" (gadget
#        role) and i2c1 on the repurposed SDIO1 pads for the slave EEPROM.
# nanokvm.cfg is merged in do_configure:append below.
SRC_URI:append:sg2002-licheervnano = " \
    file://0001-apply-dts-usb-dev.patch \
    file://0002-riscv-dts-sophgo-add-cv1800b-usb2-phy.patch \
    file://0003-nanokvm-board-dts.patch \
    file://0005-nanokvm-cv1800-reboot.patch \
    file://nanokvm.cfg \
    "

# Merge the NanoKVM fragment on top of the mainline defconfig. Plain `inherit
# kernel` (not kernel-yocto) does not auto-apply .cfg fragments, so do it here;
# do_compile's syncconfig then resolves dependencies.
#
# merge_config.sh -m warns (does not fail) when a symbol it was asked to set
# ends up at a different value because kconfig dependencies overrode it. Those
# warnings matter here: a silently-dropped "is not set" is exactly how the Image
# regrows. Promote them to a build failure.
do_configure:append:sg2002-licheervnano() {
    "${S}/scripts/kconfig/merge_config.sh" -m -O "${B}" \
        "${B}/.config" "${WORKDIR}/nanokvm.cfg" 2>&1 | tee "${B}/merge_config.log"
    oe_runmake -C ${S} O=${B} olddefconfig

    if grep -q '^Value requested for' "${B}/merge_config.log"; then
        bbwarn "nanokvm.cfg: kconfig overrode requested values:"
        bbwarn "$(grep '^Value requested for' ${B}/merge_config.log)"
    fi
}

# U-Boot stages the raw Image at kernel_addr_r=0x81000000 and the DTB at
# fdt_addr_r=0x82000000 (the stock licheerv_nano.h values). The Image must not
# reach the DTB, and neither must the kernel's runtime footprint after booti
# relocates it to 0x80200000 -- so the binding constraint is the Image header's
# image_size (text+data+bss), not the file size. Check the stricter of the two
# against a 14 MiB budget, leaving 2 MiB of headroom under the 16 MiB window.
#
# Without this the failure mode is a silent, intermittent brick: U-Boot happily
# overwrites the DTB and the kernel dies before the console is up.
KERNEL_IMAGE_MAXSIZE_BYTES = "14680064"

do_compile:append:sg2002-licheervnano() {
    kimage="${B}/arch/riscv/boot/Image"
    [ -f "${kimage}" ] || kimage="$(find ${B} -path '*/arch/riscv/boot/Image' -type f | head -1)"
    [ -f "${kimage}" ] || bbfatal "kernel size check: no Image found under ${B}"

    filesz=$(stat -c %s "${kimage}")

    # RISC-V Image header (Documentation/riscv/boot-image-header.rst):
    #   u32 code0; u32 code1; u64 text_offset; u64 image_size; ...
    # image_size is a little-endian u64 at byte offset 16.
    imagesz=$(od -An -tu8 -j16 -N8 "${kimage}" | tr -d ' ')

    bbplain "kernel Image: file=$((filesz / 1024)) KiB  image_size=$((imagesz / 1024)) KiB  budget=$((${KERNEL_IMAGE_MAXSIZE_BYTES} / 1024)) KiB"

    for sz in "${filesz}" "${imagesz}"; do
        if [ "${sz}" -gt "${KERNEL_IMAGE_MAXSIZE_BYTES}" ]; then
            bbfatal "kernel too large: ${sz} B > ${KERNEL_IMAGE_MAXSIZE_BYTES} B budget." \
                    "It would overrun fdt_addr_r=0x82000000 when U-Boot stages it at" \
                    "kernel_addr_r=0x81000000. Trim nanokvm.cfg, or raise the load" \
                    "addresses again in the u-boot recipe."
        fi
    done
}
