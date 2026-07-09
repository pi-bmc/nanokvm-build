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
           file://0002-licheerv-nano-raise-load-addrs.patch \
           "
# v2026.07
SRCREV = "ece349ade2973e220f524ce59e59711cc919263f"

# 0001-mmc-cv1800b-honor-no-1-8-v.patch: SD-card init fix. The cv1800b_sdhci
# driver lets sdhci_setup_cfg() re-add UHS caps from the controller even with
# no-1-8-v in DT, so U-Boot tries a 1.8V switch this 3.3V-only board can't do
# and the card wedges ("Card did not respond to voltage select! : -110"). The
# upstream fix merged AFTER the v2026.07 tag (lands in v2026.10), so backport it.
# Drop this patch when bumping to a U-Boot release that contains it.

DEPENDS += "bc-native dtc-native"

# U-Boot 2026.07 builds the mkeficapsule host tool against gnutls PKCS#11
# (gnutls_pkcs11_*), but oe-core's gnutls-native is built without p11-kit/PKCS#11,
# so the tool fails to link. This board boots via FSBL -> OpenSBI -> U-Boot ->
# extlinux and needs no EFI capsule update tooling; disable the host tool. (The
# EFI_LOADER runtime support in the U-Boot binary itself is untouched.)
do_configure:append() {
    "${S}/scripts/config" --file "${B}/.config" --disable TOOLS_MKEFICAPSULE
    oe_runmake -C ${S} O=${B} olddefconfig
}

COMPATIBLE_MACHINE = "sg2002-licheervnano"
