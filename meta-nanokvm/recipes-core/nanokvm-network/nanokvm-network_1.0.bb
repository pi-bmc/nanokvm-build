SUMMARY = "NanoKVM ethernet: per-unit MAC override for eth0"
DESCRIPTION = "Installs an ifupdown pre-up hook that applies a /boot/eth.mac \
override to eth0 before it is brought up, and thus before DHCP. The default \
stable MAC no longer originates here: U-Boot CRCs the eFUSE and writes \
local-mac-address into the kernel device tree (0005-licheerv-nano-efuse-mac), \
which stmmac consumes directly. U-Boot applies that unconditionally and honours \
no override, so this hook exists solely to let an operator pin a specific \
address. DHCP is handled by the stock ifupdown 'iface eth0 inet dhcp' stanza."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = "file://nanokvm-mac"
S = "${WORKDIR}"

# ip (iproute2) to set the address; busybox provides the udhcpc that ifupdown's
# dhcp method runs. init-ifupdown owns /etc/network/interfaces. The sha256sum
# dependency is gone with the eFUSE-derivation fallback.
RDEPENDS:${PN} = "iproute2 busybox-udhcpc"

do_install() {
    install -d ${D}${sysconfdir}/network/if-pre-up.d
    install -m 0755 ${WORKDIR}/nanokvm-mac \
        ${D}${sysconfdir}/network/if-pre-up.d/nanokvm-mac
}

FILES:${PN} = "${sysconfdir}/network/if-pre-up.d/nanokvm-mac"
