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
#        (0007 and 0008 are gone: an out-of-tree PHY driver for the internal
#        EPHY, and the DTS node that declared it with a hardcoded id. Ethernet
#        is back to upstream code only -- U-Boot's board_init() runs the vendor
#        bring-up (meta-sophgo's 0004, exactly what milkv_duo does) and writes
#        MII_PHYSID1/2, so phylib reads a real id off the mdio-mux and binds
#        genphy to upstream's own "ethernet-phy-ieee802.3-c22" internal_ephy in
#        cv180x.dtsi. Neither patch was what kept eth0 from appearing: the MAC
#        never probed at all because CONFIG_DWMAC_GENERIC was a module. See
#        nanokvm.cfg.)
#   0009 restores drivers/staging/android/ion (deleted upstream in 5.11) with
#        CVITek's heaps, ported to 6.18. It is the floor of the local HDMI
#        capture pipeline: soph_base/vb, soph_mipi_rx, soph_vi, soph_vpss and
#        the WAVE4 VPU in cvi_vc_drv all allocate through it via
#        sys_ion_alloc()/sys_ion_free(), and mainline has no replacement for
#        any of that stack.
#
#        It must be built in, not loaded as a module, and that is forced rather
#        than chosen: ION calls cma_alloc(), cma_release(), cma_get_name(),
#        cma_for_each_area(), plist_add() and arch_sync_dma_for_device(), none
#        of which mainline exports to modules -- as a .ko every one of them is
#        undefined at modpost. Hence a kernel patch here and not a module
#        recipe. Touches only drivers/staging, so it is independent of the
#        0001/0003 DTS ordering above.
#   0010 exports arch_sync_dma_for_device()/_for_cpu(). The same multimedia
#        modules do their own cache maintenance against physical addresses
#        (sys_cache_flush()/sys_cache_invalidate(), and the vpss CMDQ path);
#        mainline exports neither, and the dma_sync_single_for_*() alternative
#        needs a struct device and dma_addr_t that these ION-carveout physical
#        addresses do not carry. Without it cv181x_sys and cv181x_vpss fail at
#        modpost. Independent of everything above.
#   0011 declares the multimedia devices themselves: base, sys, cif (the CSI-2
#        receiver), vi, vpss, vcodec, jpu, cvi_vc_drv, plus the ION carveout
#        they allocate from. Without it the soph-media modules build but never
#        probe. MUST follow 0003 -- it appends to the same board .dts 0003 is
#        the last patch to edit -- and its ION carveout address must stay in
#        sync with meta-sophgo's cvi_board_memmap.h (CVIMMAP_ION_ADDR/_SIZE),
#        which is also what the FSBL and U-Boot reserve, and with the
#        bootm_size cap in u-boot's 0006: U-Boot relocates the initrd and FDT
#        to the top of usable DRAM, which lands inside this carveout and makes
#        the kernel reject the no-map reservation outright.
#   0012 enables i2c4, the LT6911C HDMI bridge's control bus, and pins the i2c
#        bus numbers with aliases. Without it the board has exactly one i2c bus
#        and nothing can reach the bridge to read the input resolution or write
#        EDID, so capture cannot be brought up at all. MUST follow 0003 and
#        0011 -- it appends to the same board .dts they edit.
# nanokvm.cfg is merged in do_configure:append below.
SRC_URI:append:sg2002-licheervnano = " \
    file://0001-apply-dts-usb-dev.patch \
    file://0002-riscv-dts-sophgo-add-cv1800b-usb2-phy.patch \
    file://0003-nanokvm-board-dts.patch \
    file://0005-nanokvm-cv1800-reboot.patch \
    file://0006-usb-dwc2-cv1800-enable-gadget-dma.patch \
    file://0009-staging-android-restore-ion-cvitek.patch \
    file://0010-riscv-export-arch-sync-dma.patch \
    file://0011-riscv-dts-sophgo-cv181x-multimedia.patch \
    file://0012-riscv-dts-sophgo-nanokvm-enable-i2c4.patch \
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

# The kernel now ships inside a FIT (nanokvm-boot-fit) that U-Boot stages at
# kernel_addr_r=0x83000000 and extracts down to the 0x80200000 run address, so
# the staging address no longer bounds the kernel. What bounds it is the FIT's
# own FDT load address, fdt_addr_r=0x82000000: the running kernel must not
# reach it. That is 30 MiB from 0x80200000; budget 28 MiB, keeping 2 MiB of
# headroom, which is double the old extlinux-era window.
#
# The binding number is the Image header's image_size (text+data+bss), not the
# file size, because it is the runtime footprint that collides. Without this
# check the failure mode is a silent, intermittent brick: the kernel overwrites
# its own device tree and dies before the console is up.
KERNEL_IMAGE_MAXSIZE_BYTES = "29360128"

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
            bbfatal "kernel too large: $sz B > $budget B budget. Running at" \
                    "0x80200000 it would overrun the FIT's FDT load address" \
                    "fdt_addr_r=0x82000000. Trim nanokvm.cfg, or re-plan the load" \
                    "addresses in u-boot patch 0009, boot.its.in and this budget" \
                    "together."
        fi
    done
}
