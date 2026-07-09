# The stock oe-core /etc/network/interfaces ships a predictable-name mapping
# stanza intended for full ifupdown:
#     # Busybox ifupdown won't process /en* correctly
#     auto /en*=eth
#     iface eth inet dhcp
# BusyBox ifupdown (what this image uses) mis-parses it at boot and prints
#     Error: argument "/en*" is wrong: "dev" not a valid ifname
# eth0 is configured by nanokvm-network (S30eth) and usb0 by the USB gadget, so
# the /en* mapping is unused here -- strip it (leaving lo and the rest intact).
do_install:append() {
    if [ -f "${D}${sysconfdir}/network/interfaces" ]; then
        sed -i \
            -e '/Busybox ifupdown won/d' \
            -e '\,^auto /en\*=eth,d' \
            -e '/^iface eth inet dhcp$/d' \
            "${D}${sysconfdir}/network/interfaces"
    fi
}
