# The stock ntp.conf only lists the undisciplined local clock (server
# 127.127.1.0), so ntpd never syncs real time -- it just trusts whatever the
# system clock already is. The LicheeRV Nano / NanoKVM has no battery-backed
# RTC, so it powers up at the RTC epoch (~2018) and stays there, which breaks
# TLS (x509 "certificate is not yet valid"), log timestamps, and kvmapp's
# firmware download. Add the public NTP pool so ntpd (started with -g, which
# permits the initial large step) disciplines the clock once the network is up.
# The kernel then keeps the cv1800 RTC updated via the 11-minute mode
# (RTC_SYSTOHC), so subsequent warm boots start with a sane time.

do_install:append() {
    if [ -f "${D}${sysconfdir}/ntp.conf" ]; then
        printf '\n# Public NTP pool (NanoKVM has no battery RTC; needs network time sync)\nserver 0.pool.ntp.org iburst\nserver 1.pool.ntp.org iburst\nserver 2.pool.ntp.org iburst\nserver 3.pool.ntp.org iburst\n' \
            >> "${D}${sysconfdir}/ntp.conf"
    fi
}
