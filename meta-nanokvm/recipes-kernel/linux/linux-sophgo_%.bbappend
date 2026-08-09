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
#   0007 the internal EPHY driver: the full CVITEK bring-up sequence, moved
#        here from U-Boot (meta-sophgo's 0004 no longer calls it) so the PHY's
#        analog front end stays powered down for the whole of boot and only
#        comes up when the interface is opened. Enabled by CONFIG_CV1800B_PHY.
#   0008 declares the PHY in the board DTS. Load-bearing, not a convenience:
#        with U-Boot no longer writing MII_PHYSID1/2 there is no id to probe,
#        so without it phylib creates no device and eth0 never opens. Must
#        follow 0003, the last patch to touch the board .dts.
# nanokvm.cfg is merged in do_configure:append below.
SRC_URI:append:sg2002-licheervnano = " \
    file://0001-apply-dts-usb-dev.patch \
    file://0002-riscv-dts-sophgo-add-cv1800b-usb2-phy.patch \
    file://0003-nanokvm-board-dts.patch \
    file://0005-nanokvm-cv1800-reboot.patch \
    file://0006-usb-dwc2-cv1800-enable-gadget-dma.patch \
    file://0007-net-phy-sophgo-cv1800b-internal-ephy.patch \
    file://0008-riscv-dts-sophgo-nanokvm-declare-cv1800b-ephy.patch \
    file://nanokvm.cfg \
    "

# Merge the NanoKVM fragment on top of the mainline defconfig. Plain `inherit
# kernel` (not kernel-yocto) does not auto-apply .cfg fragments, so do it here;
# do_compile's syncconfig then resolves dependencies.
#
# Then verify the fragment actually took. This check is not paranoia -- kconfig
# silently ignored three requests on the first run of this recipe:
#
#   * `select` outranks an explicit n. CONFIG_PORTABLE (default !NONPORTABLE)
#     does `select EFI`, so "# CONFIG_EFI is not set" was discarded.
#   * an unmet `depends on` makes a symbol invisible, and a request to enable an
#     invisible symbol is dropped without a message. CONFIG_NFT_NAT depends on
#     NF_TABLES_IPV4 || NF_TABLES_IPV6; CONFIG_USB_CONFIGFS_F_UVC depends on
#     VIDEO_DEV.
#
# merge_config.sh's own post-check would have caught these, but `-m` (merge
# only) skips it, and its "Value of X is redefined by fragment" lines are just
# informational -- they fire for every intentional override. So compare the
# fragment against the *final* .config ourselves, after olddefconfig has run.
#
# NB: no ${braces} on shell locals anywhere in these tasks -- bitbake parses the
# body for datastore references and would try to expand them. Same reason there
# is no $(( )) arithmetic (it raises NotImplementedError at parse time).
do_configure:append:sg2002-licheervnano() {
    "${S}/scripts/kconfig/merge_config.sh" -m -O "${B}" \
        "${B}/.config" "${WORKDIR}/nanokvm.cfg"
    oe_runmake -C ${S} O=${B} olddefconfig

    cfg="${B}/.config"
    frag="${WORKDIR}/nanokvm.cfg"
    mismatch="${B}/nanokvm-cfg-mismatch.txt"
    : > "$mismatch"

    # Requested OFF, spelled either "# CONFIG_X is not set" or "CONFIG_X=n" --
    # kconfig always writes the former back, so the latter can never match
    # literally. Both mean the same thing; check them the same way. A symbol
    # absent from .config entirely is fine: absent means off.
    { sed -n 's/^# \(CONFIG_[A-Za-z0-9_]*\) is not set$/\1/p' "$frag"
      sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)=n$/\1/p' "$frag"
    } | sort -u | while read -r sym; do
        if grep -q "^$sym=" "$cfg"; then
            echo "$sym: asked for OFF, got $(grep -m1 "^$sym=" $cfg)" >> "$mismatch"
        fi
    done

    # Requested a value but the final .config disagrees (or dropped it).
    sed -n 's/^\(CONFIG_[A-Za-z0-9_]*=.*\)$/\1/p' "$frag" | grep -v '=n$' | while read -r want; do
        if ! grep -qxF "$want" "$cfg"; then
            sym=$(echo "$want" | cut -d= -f1)
            got=$(grep -m1 "^$sym=" "$cfg" || echo "<unset/invisible>")
            echo "$sym: asked for $want, got $got" >> "$mismatch"
        fi
    done

    if [ -s "$mismatch" ]; then
        while read -r l; do bbwarn "nanokvm.cfg: $l"; done < "$mismatch"
        bbfatal "nanokvm.cfg: kconfig did not honour the requests above." \
                "Either a select forces the symbol on, or an unmet depends-on" \
                "makes it invisible. Fix the fragment; do not ignore this."
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

# NB: no $(( )) arithmetic and no ${braces} on shell locals below. bitbake parses
# shell task bodies to harvest variable references: it raises
# NotImplementedError on "$((", and it would try to resolve "${filesz}" as a
# datastore variable. Plain $var and expr(1) keep the parser happy.
do_compile:append:sg2002-licheervnano() {
    kimage="${B}/arch/riscv/boot/Image"
    [ -f "$kimage" ] || kimage="$(find ${B} -path '*/arch/riscv/boot/Image' -type f | head -1)"
    [ -f "$kimage" ] || bbfatal "kernel size check: no Image found under ${B}"

    filesz=$(stat -c %s "$kimage")

    # RISC-V Image header (Documentation/arch/riscv/boot-image-header.rst):
    #   u32 code0; u32 code1; u64 text_offset; u64 image_size; ...
    # image_size (text+data+bss) is a little-endian u64 at byte offset 16. It is
    # the larger of the two numbers and the one that actually has to clear
    # fdt_addr_r once booti relocates the kernel to 0x80200000.
    imagesz=$(od -An -tu8 -j16 -N8 "$kimage" | tr -d ' ')
    budget="${KERNEL_IMAGE_MAXSIZE_BYTES}"

    bbplain "kernel Image: file=$(expr $filesz / 1024) KiB  image_size=$(expr $imagesz / 1024) KiB  budget=$(expr $budget / 1024) KiB"

    for sz in "$filesz" "$imagesz"; do
        if [ "$sz" -gt "$budget" ]; then
            bbfatal "kernel too large: $sz B > $budget B budget. It would overrun" \
                    "fdt_addr_r=0x82000000 when U-Boot stages the Image at" \
                    "kernel_addr_r=0x81000000. Trim nanokvm.cfg, or restore the raised" \
                    "load addresses in the u-boot recipe."
        fi
    done
}
