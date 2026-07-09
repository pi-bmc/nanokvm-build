# The stock oe-core /etc/network/interfaces ships a predictable-name mapping
# stanza intended for full ifupdown:
#     # Busybox ifupdown won't process /en* correctly
#     auto /en*=eth
#     iface eth inet dhcp
# BusyBox ifupdown (what this image uses) mis-parses it at boot and prints
#     Error: argument "/en*" is wrong: "dev" not a valid ifname
# usb0 is configured by the USB gadget and the /en* mapping is unused here, so
# strip it (leaving lo, eth0 and the rest intact).
#
# eth0 keeps its stock stanza:
#     auto eth0
#     iface eth0 inet dhcp
# ifupdown does the DHCP; nanokvm-network installs an if-pre-up.d hook that sets
# a stable eFUSE-derived MAC before ifup runs udhcpc, so the lease/IP do not
# cycle regardless of who triggers ifup.
do_install:append() {
    if [ -f "${D}${sysconfdir}/network/interfaces" ]; then
        sed -i \
            -e '/Busybox ifupdown won/d' \
            -e '\,^auto /en\*=eth,d' \
            -e '/^iface eth inet dhcp$/d' \
            "${D}${sysconfdir}/network/interfaces"
    fi
}
