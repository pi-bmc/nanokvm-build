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
           "

# 0002-licheerv-nano-raise-load-addrs.patch is gone. It existed because the old
# ~28 MiB FIT (boot.sd) staged at the stock kernel_addr_r=0x81000000 overlapped
# the kernel's own 0x80200000 run region, so bootm aborted with "new format
# image overwritten". The kernel is now trimmed to fit the stock window
# (kernel_addr_r=0x81000000, fdt_addr_r=0x82000000, scriptaddr=0x80c00000) and
# is loaded as a raw Image via booti, so the upstream addresses stand. The
# linux-sophgo bbappend enforces the size budget that makes this safe.
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

    oe_runmake -C ${S} O=${B} olddefconfig
}

COMPATIBLE_MACHINE = "sg2002-licheervnano"
