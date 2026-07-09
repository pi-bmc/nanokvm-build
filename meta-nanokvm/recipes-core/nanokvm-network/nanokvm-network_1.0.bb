SUMMARY = "NanoKVM ethernet: stable eFUSE-derived MAC for eth0"
DESCRIPTION = "Installs an ifupdown pre-up hook that sets a stable, per-board \
MAC on eth0 before it is brought up, so the DHCP lease (and IP) no longer cycle \
(mainline U-Boot/stmmac provide no MAC, so the kernel would otherwise assign a \
random one each boot). DHCP itself is handled by the stock ifupdown 'iface eth0 \
inet dhcp' stanza; this recipe only owns the MAC."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = "file://nanokvm-mac"
S = "${WORKDIR}"

# ip (iproute2) to set the address; busybox provides sha256sum + the udhcpc that
# ifupdown's dhcp method runs. init-ifupdown owns /etc/network/interfaces.
RDEPENDS:${PN} = "iproute2 busybox-udhcpc"

do_install() {
    install -d ${D}${sysconfdir}/network/if-pre-up.d
    install -m 0755 ${WORKDIR}/nanokvm-mac \
        ${D}${sysconfdir}/network/if-pre-up.d/nanokvm-mac
}

FILES:${PN} = "${sysconfdir}/network/if-pre-up.d/nanokvm-mac"
