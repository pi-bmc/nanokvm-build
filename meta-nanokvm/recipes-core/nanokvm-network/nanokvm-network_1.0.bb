SUMMARY = "NanoKVM networking glue: eth0 MAC override, usb0 host-link isolation"
DESCRIPTION = "Two jobs. (1) An ifupdown pre-up hook that applies a \
/boot/eth.mac override to eth0 before it is brought up, and thus before DHCP. \
The default stable MAC no longer originates here: U-Boot CRCs the eFUSE and \
writes local-mac-address into the kernel device tree \
(0005-licheerv-nano-efuse-mac), which stmmac consumes directly. U-Boot applies \
that unconditionally and honours no override, so this hook exists solely to \
let an operator pin a specific address. DHCP is handled by the stock ifupdown \
'iface eth0 inet dhcp' stanza. (2) The Redfish-Host-Interface-style handling \
of the USB gadget NIC (ecm/ncm usb0): if-up.d/if-post-down.d hooks that run \
a single-lease udhcpd for the managed host (no router/DNS options) and pin \
the isolation knobs (no forwarding, no accept_ra, an nftables guard keeping \
usb0 out of the forward path), paired with the gateway-less link-local stanza \
the init-ifupdown bbappend writes into /etc/network/interfaces."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = " \
    file://nanokvm-mac \
    file://nanokvm-usb0-up \
    file://nanokvm-usb0-down \
    file://udhcpd-usb0.conf \
"
S = "${WORKDIR}"

# ip (iproute2) to set the address; busybox provides the udhcpc that ifupdown's
# dhcp method runs and the udhcpd the usb0 hook starts (udhcpd lives in the
# main busybox package, not a split one). init-ifupdown owns
# /etc/network/interfaces. The nft guard degrades gracefully when nftables is
# absent, so it stays a recommendation rather than a dependency.
RDEPENDS:${PN} = "iproute2 busybox-udhcpc"
RRECOMMENDS:${PN} = "nftables"

do_install() {
    install -d ${D}${sysconfdir}/network/if-pre-up.d
    install -m 0755 ${WORKDIR}/nanokvm-mac \
        ${D}${sysconfdir}/network/if-pre-up.d/nanokvm-mac

    install -d ${D}${sysconfdir}/network/if-up.d
    install -m 0755 ${WORKDIR}/nanokvm-usb0-up \
        ${D}${sysconfdir}/network/if-up.d/nanokvm-usb0

    install -d ${D}${sysconfdir}/network/if-post-down.d
    install -m 0755 ${WORKDIR}/nanokvm-usb0-down \
        ${D}${sysconfdir}/network/if-post-down.d/nanokvm-usb0

    install -m 0644 ${WORKDIR}/udhcpd-usb0.conf \
        ${D}${sysconfdir}/udhcpd-usb0.conf
}

FILES:${PN} = " \
    ${sysconfdir}/network/if-pre-up.d/nanokvm-mac \
    ${sysconfdir}/network/if-up.d/nanokvm-usb0 \
    ${sysconfdir}/network/if-post-down.d/nanokvm-usb0 \
    ${sysconfdir}/udhcpd-usb0.conf \
"
