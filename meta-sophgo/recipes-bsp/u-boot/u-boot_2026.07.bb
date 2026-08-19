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
           file://0003-licheerv-nano-cap-sdhci0-to-default-speed.patch \
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

    # --- Persistent, crash-safe environment + A/B bootcount -----------------
    #
    # Stock licheerv_nano has no environment at all ("Loading Environment from
    # nowhere"), so there is nowhere for A/B state to live. Put it in raw MMC
    # sectors in the 1 MiB gap between the MBR and p1 (verified all-zero; the
    # boot ROM only reads the first ~43 KiB of that gap), NOT in a filesystem:
    # a raw redundant env is two copies with a validity flag, switched
    # atomically, so a power cut mid-write always leaves one valid copy. That is
    # the one piece of mutable boot state, and it is crash-safe by construction.
    #
    #   0x80000 (512 KiB) primary env    64 KiB
    #   0x90000 (576 KiB) redundant env  64 KiB
    #   ends at 0xA0000 (640 KiB), clear of p1 at 0x100000 (1 MiB)
    #
    # The image pre-seeds the primary copy (nanokvm-uboot-env), so a freshly
    # flashed card boots slot a with a valid CRC and no warning.
    "${S}/scripts/config" --file "${B}/.config" --disable ENV_IS_NOWHERE
    "${S}/scripts/config" --file "${B}/.config" --enable  ENV_IS_IN_MMC
    "${S}/scripts/config" --file "${B}/.config" --enable  ENV_REDUNDANT
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_SIZE 0x10000
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_OFFSET 0x80000
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_OFFSET_REDUND 0x90000
    "${S}/scripts/config" --file "${B}/.config" --set-val ENV_MMC_DEVICE_INDEX 0
    "${S}/scripts/config" --file "${B}/.config" --set-val SYS_MMC_ENV_DEV 0

    # BOOTCOUNT_ENV only persists the counter while upgrade_available is set,
    # which userspace sets only around an update. Normal boots therefore write
    # nothing at all -- the boot path stays read-only in steady state, and the
    # counter costs an env write only while an update is on probation.
    "${S}/scripts/config" --file "${B}/.config" --enable  BOOTCOUNT_LIMIT
    "${S}/scripts/config" --file "${B}/.config" --enable  BOOTCOUNT_ENV

    # Boot straight to the A/B FIT flow instead of scanning for extlinux. One
    # fatload of one hash-verified file is the smallest job we can hand U-Boot
    # on a board whose SD reads are marginal. bootstd stays compiled in, so
    # `bootflow scan` is still there to rescue a board by hand.
    "${S}/scripts/config" --file "${B}/.config" --disable BOOTSTD_BOOTCOMMAND
    "${S}/scripts/config" --file "${B}/.config" --enable  USE_BOOTCOMMAND
    "${S}/scripts/config" --file "${B}/.config" --set-str BOOTCOMMAND "run nkvm_boot"

    oe_runmake -C ${S} O=${B} olddefconfig
}

COMPATIBLE_MACHINE = "sg2002-licheervnano"
