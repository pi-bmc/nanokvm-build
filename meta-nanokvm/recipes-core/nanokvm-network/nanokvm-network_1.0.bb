SUMMARY = "NanoKVM ethernet bring-up (DHCP + reserve address)"
DESCRIPTION = "sysvinit service that configures eth0 for the NanoKVM BMC: DHCP \
by default (udhcpc in the background), static config from /boot/eth.nodhcp when \
present, and a fixed reserve address (192.168.90.1/22) fallback so the KVM is \
always reachable. Mirrors pi-bmc/nanokvm-app packaging/etc/init.d/S30eth. \
The previous (sipeed) app configured the network internally; the refactored \
pi-bmc app expects this external script."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = "file://nanokvm-eth"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "nanokvm-eth"
# Bring the network up after udev/device nodes are ready; tear down late.
INITSCRIPT_PARAMS = "start 30 2 3 4 5 . stop 70 0 1 6 ."

# ip (iproute2), udhcpc + its default.script (busybox-udhcpc), arping for the
# static-config duplicate-address check (iputils-arping).
RDEPENDS:${PN} = "iproute2 busybox-udhcpc iputils-arping"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nanokvm-eth ${D}${sysconfdir}/init.d/nanokvm-eth
}

FILES:${PN} = "${sysconfdir}/init.d/nanokvm-eth"
