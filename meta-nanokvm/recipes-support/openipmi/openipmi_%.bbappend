# The NanoKVM BMC config is provided by the ipmi-sim recipe at /etc/ipmi/lan.conf
# (its S99ipmi_sim service points there). openipmi ships a generic default at the
# same path, which clashes at do_rootfs (opkg refuses overlapping files). In the
# original buildroot build the rootfs overlay simply overwrote it; drop openipmi's
# copy here so ipmi-sim's NanoKVM config is the canonical one.
do_install:append() {
    rm -f ${D}${sysconfdir}/ipmi/lan.conf
}
