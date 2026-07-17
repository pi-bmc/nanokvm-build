# The stock oe-core /etc/network/interfaces needs two edits for this image.
#
# 1. It ships a predictable-name mapping stanza intended for full ifupdown:
#        # Busybox ifupdown won't process /en* correctly
#        auto /en*=eth
#        iface eth inet dhcp
#    BusyBox ifupdown (what this image uses) mis-parses it at boot and prints
#        Error: argument "/en*" is wrong: "dev" not a valid ifname
#    The /en* mapping is unused here, so strip it.
#
# 2. Its usb0 stanza is actively harmful on a BMC:
#        iface usb0 inet static
#            address 192.168.7.2 ... gateway 192.168.7.1
#    udev-extraconf's autonet.rules runs `ifup usb0` the moment the gadget's
#    ecm/ncm function registers the netdev (which happens at every boot --
#    usb.ecm0 ships on the boot partition), and that stanza then installs
#    `default via 192.168.7.1 dev usb0` on the BMC. Depending on how the race
#    against eth0's DHCP resolves, the BMC's default route points into the
#    point-to-point USB link and everything off-subnet (DNS, NTP, the gateway)
#    blackholes; 192.168.7.0/24 also collides with real RFC1918 LANs. Replace
#    it with a Redfish-Host-Interface-style (DSP0270) stanza: link-local
#    addressing, NO gateway, so the link can never influence BMC routing. The
#    /16 mask (per RFC 3927) keeps the BMC reachable even from a host whose
#    DHCP client fell back to a random IPv4LL 169.254.x.y self-assignment.
#
# eth0 keeps its stock stanza:
#     auto eth0
#     iface eth0 inet dhcp
# ifupdown does the DHCP; nanokvm-network installs an if-pre-up.d hook that sets
# a stable eFUSE-derived MAC before ifup runs udhcpc, so the lease/IP do not
# cycle regardless of who triggers ifup. nanokvm-network also owns the usb0
# if-up.d/if-post-down.d hooks (single-lease DHCP server for the host, and the
# forwarding/RA isolation) referenced by the stanza appended below.
do_install:append() {
    if [ -f "${D}${sysconfdir}/network/interfaces" ]; then
        sed -i \
            -e '/Busybox ifupdown won/d' \
            -e '\,^auto /en\*=eth,d' \
            -e '/^iface eth inet dhcp$/d' \
            -e '/^# Ethernet\/RNDIS gadget/,/^[[:space:]]*gateway 192\.168\.7\.1/d' \
            "${D}${sysconfdir}/network/interfaces"
        cat >>"${D}${sysconfdir}/network/interfaces" <<'EOF'
# BMC <-> managed-host USB gadget NIC (ecm/ncm usb0), modeled on the Redfish
# Host Interface (DSP0270) conventions: a point-to-point management link on
# IPv4 link-local addressing. Deliberately no gateway -- the BMC's default
# route must always stay on eth0 -- and the host is served a single DHCP lease
# with no router/DNS options so it never routes LAN traffic here either.
# Brought up by udev-extraconf's autonet rule when the gadget function
# registers the netdev; nanokvm-network's if-up.d/if-post-down.d hooks start
# and stop the DHCP server and pin the isolation knobs.
iface usb0 inet static
	address 169.254.10.1
	netmask 255.255.0.0
EOF
    fi
}
