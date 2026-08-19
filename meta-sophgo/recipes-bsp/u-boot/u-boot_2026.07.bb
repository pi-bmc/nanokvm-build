SUMMARY = "Mainline U-Boot for Sophgo SG2002 — LicheeRV Nano"
DESCRIPTION = "Mainline U-Boot with upstream LicheeRV Nano support \
(sipeed_licheerv_nano_defconfig, doc/board/sophgo/licheerv_nano.rst). It runs \
in S-mode on top of the vendor FSBL (DDR/clock init) and mainline OpenSBI \
fw_dynamic; the fsbl recipe packs all three into fip.bin."

require recipes-bsp/u-boot/u-boot-common.inc
require recipes-bsp/u-boot/u-boot.inc

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Override oe-core's pinned 2024.01 source (and its CVE patch, which does not
# apply here): the LicheeRV Nano board landed upstream after 2024.01.
SRC_URI = "git://source.denx.de/u-boot/u-boot.git;protocol=https;branch=master \
           file://0001-mmc-cv1800b-honor-no-1-8-v.patch \
           file://0010-mmc-cv1800b-sdhci-program-phy-at-init.patch \
           file://0004-licheerv-nano-init-internal-ephy.patch \
           file://0005-licheerv-nano-efuse-mac.patch \
           file://0006-licheerv-nano-ramdisk-addr.patch \
           file://0008-licheerv-nano-mux-i2c4.patch \
           file://0009-licheerv-nano-ab-boot-env.patch \
           "

# 0002-licheerv-nano-raise-load-addrs.patch is gone; 0009 supersedes it. 0002
# existed because the old ~28 MiB FIT (boot.sd) staged at the stock
# kernel_addr_r=0x81000000 overlapped the kernel's own 0x80200000 run region, so
# bootm aborted with "new format image overwritten". The image is back on a FIT
# (boot_<slot>.itb, kernel+FDT+initramfs), so staging must again sit clear of the
# run region: 0009 moves kernel_addr_r to 0x83000000 and adds the A/B env. The
# kernel budget is now bounded by fdt_addr_r=0x82000000 (30 MiB from the
# 0x80200000 run address) rather than by the staging address; the linux-sophgo
# bbappend asserts it.
#
# 0003-licheerv-nano-cap-sdhci0-to-default-speed.patch is gone; 0010 replaces
# it. 0003 put max-frequency = <25000000> in U-Boot's copy of the board DTS to
# work around unreliable SD reads. It could not have worked: mmc_of_parse()
# parses the property into cfg->f_max and sdhci_setup_cfg() then overwrites
# cfg->f_max with host->max_clk, because the driver passed f_max = 0. The bus
# ran at SD high-speed the whole time; whatever made the intermittent failures
# go away, it was not that property.
#
# The real cause was an unprogrammed PHY: U-Boot only wrote the tap delay from
# platform_execute_tuning(), which no-1-8-v means never runs. 0010 programs the
# CV18xx PHY at init the way Linux does -- which is why the kernel reads the
# same card at 50 MHz SD high-speed without trouble -- and, separately, makes
# max-frequency actually take effect. If some card still misbehaves, adding the
# property back to the board DTS is now a cap that works.
#
# Patch numbering is left as-is rather than resequenced, so the remaining files
# keep matching the commit history that introduced them.
# v2026.07
SRCREV = "ece349ade2973e220f524ce59e59711cc919263f"

# 0001-mmc-cv1800b-honor-no-1-8-v.patch: SD-card init fix. The cv1800b_sdhci
# driver lets sdhci_setup_cfg() re-add UHS caps from the controller even with
# no-1-8-v in DT, so U-Boot tries a 1.8V switch this 3.3V-only board can't do
# and the card wedges ("Card did not respond to voltage select! : -110"). The
# upstream fix merged AFTER the v2026.07 tag (lands in v2026.10), so backport it.
# Drop this patch when bumping to a U-Boot release that contains it.

DEPENDS += "bc-native dtc-native"

# Keep the version string reproducible: "2026.07", never "2026.07-g<sha>" or a
# bare "2026.07+". LOCALVERSION= (set, empty) stops scripts/setlocalversion from
# appending "+" for a non-tagged tree. LOCALVERSION_AUTO=n is passed for parity
# with the kernel, but on its own it is inert -- setlocalversion greps
# CONFIG_LOCALVERSION_AUTO out of auto.conf, so the symbol is also turned off in
# do_configure:append below.
EXTRA_OEMAKE += 'LOCALVERSION_AUTO=n LOCALVERSION='

do_configure:append() {
    # mkeficapsule: U-Boot 2026.07 links its host tool against gnutls PKCS#11
    # (gnutls_pkcs11_*), which oe-core's gnutls-native lacks (no p11-kit), so it
    # fails to link. This board boots FSBL -> OpenSBI -> U-Boot -> extlinux and
    # needs no EFI capsule tooling; disable it. (Runtime EFI_LOADER is untouched.)
    "${S}/scripts/config" --file "${B}/.config" --disable TOOLS_MKEFICAPSULE

    # BOARD_INIT: sipeed_licheerv_nano_defconfig explicitly disables it, so
    # board_init() (0004: cv1800b_ephy_init + sysreset bind) is never called --
    # the internal EPHY stays in shutdown (EPHY_CTL=0x901) and Linux can't attach
    # the PHY. It defaults y on RISC-V (milkv_duo relies on that); re-enable it.
    "${S}/scripts/config" --file "${B}/.config" --enable BOARD_INIT

    # LOCALVERSION_AUTO defaults to y and makes setlocalversion run git describe
    # against ${S}, tainting the version with the checkout's SHA.
    "${S}/scripts/config" --file "${B}/.config" --disable LOCALVERSION_AUTO

    # --- Persistent, crash-safe environment ---------------------------------
    #
    # Stock licheerv_nano has no environment at all ("Loading Environment from
    # nowhere"), so there is nowhere for A/B state to live -- and bootmeth_rauc
    # below cannot run without one: distro_rauc_boot() calls env_save() and
    # aborts the boot if it fails.
    #
    # It goes in raw MMC sectors in the 1 MiB gap between the MBR and p1
    # (verified all-zero; the boot ROM only reads the first ~43 KiB of that
    # gap), NOT in a filesystem. A raw redundant env is two copies with a
    # validity flag, switched atomically, so a power cut mid-write always leaves
    # one valid copy. Every other backend this SoC could offer was surveyed and
    # none exist: no SPI NOR is populated (spi-nor@10000000 stays disabled and
    # /proc/mtd is empty on the running board), there is no eMMC, and the only
    # i2c EEPROM is the slave the board used to present outward. FAT or ext4 on
    # this same card would be strictly worse -- no journal, non-atomic, and p1
    # is what the boot ROM reads fip.bin from.
    #
    #   0x80000 (512 KiB) primary env    64 KiB
    #   0x90000 (576 KiB) redundant env  64 KiB
    #   ends at 0xA0000 (640 KiB), clear of p1 at 0x100000 (1 MiB)
    #
    # The image pre-seeds BOTH copies (nanokvm-uboot-env), so a freshly flashed
    # card starts from a known state with no bad-CRC warning, and re-flashing a
    # used card cannot leave a stale copy behind that outranks the new one.
    "${S}/scripts/config" --file "${B}/.config" --disable ENV_IS_NOWHERE
    "${S}/scripts/config" --file "${B}/.config" --enable  ENV_IS_IN_MMC
    "${S}/scripts/config" --file "${B}/.config" --enable  ENV_REDUNDANT
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_SIZE 0x10000
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_OFFSET 0x80000
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_OFFSET_REDUND 0x90000
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_MMC_DEVICE_INDEX 0

    # --- A/B slot selection: bootmeth_rauc ----------------------------------
    #
    # Upstream's RAUC bootmeth owns the slot state machine now, replacing the
    # hand-written nkvm_* env script that used to live in patch 0009. It keeps
    # BOOT_ORDER and BOOT_<slot>_LEFT in the environment, decrements the active
    # slot's counter before handing off, and falls to the next slot in the order
    # once a slot's tries run out.
    #
    # PARTITIONS is a list of boot,root pairs -- the driver is hardcoded to
    # parse pairs, so pairs have to be supplied. Both slots name p1 as their
    # boot partition: distro_rauc_scan_parts() probes a filesystem there (the
    # FAT p1 works; a raw squashfs would not), while the root partition only
    # goes through part_get_info(), an MBR table read. So the existing layout
    # works untouched and boot.scr picks its FIT from ${distro_rootpart}.
    #
    # Lowercase slot names keep every name in the system spelled the same way:
    # rauc.slot=a on the cmdline, boot_a.itb on p1, BOOT_a_LEFT in the env.
    "${S}/scripts/config" --file "${B}/.config" --enable  BOOTMETH_RAUC
    "${S}/scripts/config" --file "${B}/.config" --set-str BOOTMETH_RAUC_PARTITIONS "1,2 1,3"
    "${S}/scripts/config" --file "${B}/.config" --set-str BOOTMETH_RAUC_BOOT_ORDER "a b"
    "${S}/scripts/config" --file "${B}/.config" --set-val BOOTMETH_RAUC_DEFAULT_TRIES 3
    "${S}/scripts/config" --file "${B}/.config" --enable  BOOTMETH_RAUC_RESET_ALL_ZERO_TRIES

    # Nothing else may answer the scan -- in the order, bootmeth_script would
    # find the very same boot.scr on p1 and source it with none of the slot
    # variables set. Disabling those bootmeths here does NOT work: BOOTSTD_FULL
    # implies BOOTSTD_DEFAULTS, which selects BOOTMETH_DISTRO, which *selects*
    # BOOTMETH_SCRIPT / _EXTLINUX / _EXTLINUX_PXE / _EFILOADER -- olddefconfig
    # turns them straight back on, and dropping BOOTSTD_DEFAULTS to break the
    # chain would take CMD_FAT / CMD_EXT4 / CMD_FS_GENERIC with it.
    #
    # The order is pinned in the environment instead, with bootmeths=rauc. That
    # is the mechanism upstream provides for it: U_BOOT_ENV_CALLBACK(bootmeths)
    # calls bootmeth_set_order() as the variable is imported, and initr_dm()
    # runs before initr_env() so the bootmeth devices exist by then. The value
    # is in the *compiled* defaults (patch 0009), so it holds even on a card
    # whose stored environment is blank or corrupt. Leaving the other bootmeths
    # built in is deliberate: `bootmeth order` at the prompt is a real rescue
    # path on a board with no other one.
    "${S}/scripts/config" --file "${B}/.config" --disable BOOTMETH_VBE

    # BOOTCOUNT_LIMIT/BOOTCOUNT_ENV are gone: BOOT_<slot>_LEFT replaces the
    # bootcount/bootlimit/altbootcmd triple, and unlike it the counter is not
    # gated on an upgrade_available flag that nothing ever set.
    "${S}/scripts/config" --file "${B}/.config" --disable BOOTCOUNT_LIMIT

    # "; reset" is load-bearing. bootmeth_rauc yields one bootflow for one slot
    # and has no in-session retry, so a slot that fails to boot would otherwise
    # drop a headless BMC to the U-Boot prompt and stay there. Rebooting spends
    # one BOOT_<slot>_LEFT try per attempt, which is exactly the intended way to
    # walk through the boot order.
    "${S}/scripts/config" --file "${B}/.config" --disable BOOTSTD_BOOTCOMMAND
    "${S}/scripts/config" --file "${B}/.config" --enable  USE_BOOTCOMMAND
    "${S}/scripts/config" --file "${B}/.config" --set-str BOOTCOMMAND "bootflow scan -b; reset"

    oe_runmake -C ${S} O=${B} olddefconfig
}

COMPATIBLE_MACHINE = "sg2002-licheervnano"
