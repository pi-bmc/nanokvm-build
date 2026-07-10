# Scope mDNS/DNS-SD to the management LAN. usb0 is the point-to-point
# Redfish-Host-Interface-style link to the managed host: it has a fixed,
# well-known address (169.254.10.1), so discovery there buys nothing, and a
# BMC that announces itself on both eth0 and usb0 hands the host's mDNS stack
# duplicate records for the same name with unrelated addresses. The stock
# config ships the exact line commented out; just enable it.
do_install:append() {
    sed -i -e 's/^#allow-interfaces=eth0$/allow-interfaces=eth0/' \
        ${D}${sysconfdir}/avahi/avahi-daemon.conf
}
