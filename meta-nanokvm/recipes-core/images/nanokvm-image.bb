SUMMARY = "NanoKVM SD card image — IP KVM system for LicheeRV Nano / SG2002"
LICENSE = "MIT"

inherit core-image

# --- Root filesystem: squashfs + volatile overlay -------------------------
# The root is an immutable squashfs-zst image in an A/B slot pair (p2/p3);
# the initramfs (nanokvm-initramfs-image) lays a volatile tmpfs overlay over
# it and mounts the ext4 data partition (created on first boot from the rest
# of the card) at /var/lib/nanokvm. Everything that must survive a reboot
# lives there — see the initramfs /init for the persistence contract. A hard
# power cut can therefore never corrupt the root: every boot starts from the
# exact image bytes.
IMAGE_FSTYPES += "squashfs-zst"
IMAGE_TYPEDEP:wic = "squashfs-zst"
WKS_FILE = "nanokvm-sd.wks.in"

# Use the plain packagegroup-base (not -extended): -extended unconditionally
# RDEPENDS packagegroup-base-wifi -> wireless-regdb-static and would drag in
# wireless kernel modules. WiFi is intentionally absent from this image (the
# SDIO1 pads are repurposed as I2C1 for the I2C slave EEPROM), so packagegroup-base
# pulls the wifi subgroup only when MACHINE_FEATURES advertises it -- it does not.
CORE_IMAGE_BASE_INSTALL = "packagegroup-core-boot packagegroup-base"

# --- Features ---
# No "package-management": there is no feed to install from, and it costs the
# opkg binary plus its package database on every boot partition.
#
# No "ssh-server-openssh" (nor dropbear): the NanoKVM server IS the SSH server.
# It implements the transport in-process (nanokvm-app server/service/ssh, on
# golang.org/x/crypto/ssh) and runs sessions on the same PTY code as the web
# terminal, so SSH and the browser console are one implementation. That also
# unifies the credentials -- SSH authenticates against the BMC account (the one
# Redfish, IPMI and the web UI use) plus an authorized_keys file managed from
# the Settings dialog -- instead of a second, independent user database in
# /etc/shadow. Dropping sshd takes openssh, its sftp-server and the PAM/shadow
# glue out of the image, and removes the init script the app used to shell out
# to for the SSH on/off toggle.
#
# "debug-tweaks" is retained deliberately: it leaves root with an empty
# password, which is what makes the serial console usable for recovery on a
# board with no other provisioned credential. It no longer has anything to do
# with SSH -- there is no sshd to permit root login -- so an empty root
# password is not remotely exploitable on its own. Replace it with a real
# credential (EXTRA_USERS_PARAMS) before shipping hardware.
IMAGE_FEATURES += " \
    debug-tweaks \
    "

# Pulled in by RRECOMMENDS, not by anything here:
#   kernel-image-image  ~29 MiB -- a second copy of the kernel in the rootfs's
#                       /boot, which is then shadowed by the vfat mount over it.
#                       The kernel U-Boot actually loads lives on the FAT
#                       partition (IMAGE_BOOT_FILES), not here.
#   init-ifupdown       ifupdown /etc/network glue (packagegroup-core-boot
#                       RRECOMMENDS on sysvinit). All interface addressing --
#                       including bringing up lo, done by the initramfs -- is
#                       owned by the NanoKVM server; ifupdown would only fight
#                       it.
# busybox-udhcpc: busybox RRECOMMENDS it back despite the in-process DHCP
# client (see the network comment below). hdparm (ATA tuning on an SD-only
# board) and the e2fsprogs metapackage ride in on packagegroup-base-ext2's
# RRECOMMENDS; the e2fsck/mke2fs subpackages it hard-RDEPENDS stay.
BAD_RECOMMENDATIONS += "kernel-image-image init-ifupdown busybox-udhcpc hdparm e2fsprogs"

# --- System foundation ---
# Dropped: kmod (CONFIG_MODULES=n -- nothing to load), util-linux-rfkill (no
# radio), file (8.2 MiB magic.mgc), watchdog (no wdt node in the DTS, so
# /dev/watchdog never appears and the daemon failed on every boot).
# parted / e2fsprogs-resize2fs / exfatprogs are gone too: disk provisioning
# (data-partition creation) moved into the initramfs, which carries its own
# sfdisk/mke2fs, and the data partition is ext4 now, not exfat.
# The util-linux metapackage is gone with them (~7 MiB of subpackages, every
# exercised tool shadowed by a compiled-in busybox applet); util-linux-agetty
# stays so the serial getty keeps its current implementation. The e2fsprogs
# metapackage is gone for the same reason -- the rootfs runs no fsck/mkfs
# (the initramfs owns both); packagegroup-base-ext2 still hard-pulls the
# e2fsck/mke2fs subpackages.
# eudev + udev-extraconf are gone with the device manager (see the distro
# conf): devtmpfs provides every node, nothing hotplugs, and the automount/
# autonet udev glue had no remaining job.
IMAGE_INSTALL:append = " \
    zram-swap \
    busybox \
    bash \
    util-linux-agetty \
    "

# --- Network core ---
# Dropped: bind (a full DNS server), dnsmasq, lldpd, macchanger, android-tools,
# usbutils (no USB host), iperf3, mtr, tcpdump, traceroute, sysstat, socat,
# netcat-openbsd, rsync. None are referenced by nanokvm-server, which shells out
# only to mount, passwd, reboot, sh, sync and umount.
# mDNS (<hostname>.local) is served by nanokvm-server's built-in responder
# (server/service/mdns, a pion/mdns hostname responder scoped to eth0), so
# avahi-daemon is intentionally NOT installed. It previously only published the
# host A/AAAA record anyway (its example service files are stripped by the base
# recipe), which the in-server responder replicates.
# Interface addressing is owned entirely by nanokvm-server
# (server/service/network, netlink): eth0 static/DHCP (in-process DHCPv4
# client), the /boot/eth.mac override, and the usb0 Redfish-Host-Interface
# link (169.254.10.1/16, isolation sysctls, nft forward guard, single-lease
# in-process DHCP server for the host). That retired the nanokvm-network
# recipe (ifupdown hooks + udhcpd-usb0.conf), busybox-udhcpc (nothing runs
# udhcpc anymore) and finally ifupdown itself -- the initramfs brings lo up,
# so no /etc/network/interfaces exists at all. iproute2 stays for operator
# debugging only.
# ssdp-responder is gone (JetKVM-style minimalism): the server's built-in
# mDNS responder already provides discovery, and the BMC is always reachable
# at the well-known RHI address; a second discovery daemon earned no keep.
# ntp (ntpd + its bbappend) is gone too: clock sync is in-process now
# (server/service/timesync, a JetKVM-style SNTP client with an HTTP Date
# fallback that also honors NTP servers from the DHCP lease), so the last
# non-app network daemon and its init script left the image.
# The iputils metapackage is dropped: busybox compiles ping/ping6/traceroute,
# so only arping (no busybox applet) earns its keep.
IMAGE_INSTALL:append = " \
    iputils-arping \
    ethtool \
    iproute2 \
    "

# --- WiFi / Bluetooth ---
# Intentionally none. The SDIO1 pads are repurposed as I2C1 for the I2C slave
# EEPROM, and CONFIG_WLAN / CONFIG_WIRELESS / CONFIG_BT are off in nanokvm.cfg.

# --- VPN / firewall ---
# nftables only; the legacy iptables front-end and ppp are dropped. The kernel
# carries NF_TABLES/NF_NAT/NFT_NAT and WIREGUARD.
IMAGE_INSTALL:append = " \
    nftables \
    wireguard-tools \
    "

# --- Security / crypto ---
# haveged is gone: it predates the kernel's own jitter entropy. This kernel
# has CONFIG_CRYPTO_JITTERENTROPY=y, which seeds the crng the same way haveged
# did from userspace, so the daemon was redundant. (If the app's first-boot SSH
# host-key generation ever stalls on real hardware, that would be the reason --
# re-add it then.)
# krb5 is dropped -- nothing authenticates against a KDC.
# No SSH packages here: the server is in-process (see IMAGE_FEATURES above).
IMAGE_INSTALL:append = " \
    openssl \
    ca-certificates \
    "

# --- Compression ---
# The app serves firmware images and ISOs; it does not shell out to any archiver.
# busybox already provides gzip/tar. Everything else (bzip2, lzip, p7zip, pigz,
# unzip, zip, zstd, lzop, brotli, libzip, lz4) is dropped.

# --- Libraries ---
# Dropped: libwebsockets, nanomsg, qrencode (the Go server implements its own
# websocket and QR handling), plus tslib, libinput, libxkbcommon, fontconfig,
# freetype and hicolor-icon-theme -- an input/display/font stack on a board with
# no display, kept alive only by their own inter-dependencies.
# curl + libcurl are gone too (~2.6 MiB with their libidn2/libunistring tail):
# the Go server does its own HTTP and nothing in the image shells out to curl;
# busybox wget (HTTPS-enabled) covers ad-hoc operator fetches.

# --- Input ---
# input-event-daemon is gone: it was installed with upstream's *sample*
# config, no init script and no inittab entry — it never ran and would have
# done nothing NanoKVM-related if it had. The front-panel buttons (gpio-keys
# in the board DTS -> /dev/input/event*) belong in the NanoKVM server as an
# in-process evdev reader wired to its own power service, JetKVM-style.
# evtest and fbset are dropped.

# --- Python 3 ---
# Intentionally none. 68 python3-* packages, ~15 MiB with libpython3.12 and the
# cryptography extension, and nothing in the image invokes an interpreter: the
# server is a single static Go binary and every init script is POSIX sh.

# --- Developer / debug tools ---
# Dropped: vim, tmux, htop, strace, memtester, picocom, setserial, ser2net,
# u-boot-tools, spidev-test (no SPI node is enabled in the board DTS, so
# /dev/spidev* never appears). busybox supplies vi, top and ps.

# --- Sophgo SDK ---
# Fully removed, along with the whole meta-sophgo-sdk layer. The closed
# multimedia/AI stack (sophgo-middleware, cvi-rtsp, osdrv, maix-cdk,
# sg2002-codec-firmware) went first (proprietary ISP blobs, CLOSED cvitek
# kernel modules; the pure-Go app does not link or exec any of it), and the
# last two stragglers -- axp2101 and cvi-pinmux, CLOSED-licensed AUTOREV
# debug CLIs from the vendor osdrv tree -- were installed but never executed
# by anything: pinmuxing is owned by the DTS/pinctrl and PMIC power is not
# managed from userspace on this board. uvc-gadget is gone too -- nothing
# creates a uvc function (see nanokvm.cfg).

# --- NanoKVM application ---
# i2c-eeprom is gone: the board DTS declares both the i2c1 pinmux and the
# eeprom@50 slave node, so the kernel binds i2c-slave-eeprom at boot and the
# init script's only remaining effect was to re-apply a pinmux pinctrl had
# already set. nanokvm-gadget is deploy-only now (boot-partition files via
# do_image_wic below) -- its first-boot seeding script moved into the server.
IMAGE_INSTALL:append = " \
    nanokvm-server \
    "

# --- Raspberry Pi boot image (served to the managed Pi via the USB gadget) ---
# rpi-firmware-seed stages the aarch64 U-Boot image built by the "rpi"
# multiconfig (the vendored meta-raspberrypi layer) into
# DEPLOY_DIR_IMAGE/nkvm-data-root, which wic packs into the data partition
# (p4) as its factory content -- see wic/nanokvm-sd.wks.in. Nothing ships in
# the rootfs and nothing is copied or decompressed at runtime: a flashed card
# already holds /var/lib/nanokvm/firmware/uboot-rpi.img, and the server
# (Firmware.SeedPath fallback, then download) only rebuilds it if the data
# partition is ever lost. The do_image_wic dependency below is what pulls the
# whole rpi multiconfig into a `kas build kas.yml` -- the aarch64
# TF-A/U-Boot/RPi-overlay build and the crane-based talos-dtbs fetch run as a
# side effect of building this image.
do_image_wic[depends] += "rpi-firmware-seed:do_deploy"

# --- SD card image via WKS ---
# (No IMAGE_ROOTFS_SIZE: squashfs is content-sized; the wks pins the slot
# partitions at 512 MB and rawcopy errors out if the squashfs outgrows them.)
do_image_wic[depends] += "fsbl:do_deploy linux-sophgo:do_deploy nanokvm-initramfs-image:do_image_complete"

# --- Boot-partition config files ---
# nanokvm-gadget deploys these to DEPLOY_DIR_IMAGE; wic's bootimg-partition puts
# them on the FAT boot partition next to fip.bin, the kernel Image and the DTB.
# Read at runtime from /boot by the NanoKVM server (server/service/usbgadget
# owns the gadget now; usb.ecm0 seeds the default ECM function on first boot).
# extlinux.conf is the mainline U-Boot boot menu (bootmeth_extlinux scans
# /extlinux/extlinux.conf); it loads the initramfs (INITRD) built by
# nanokvm-initramfs-image. "slot" selects the active rootfs slot ("a" = p2).
IMAGE_BOOT_FILES:append = " board hostname.prefix ver usb.ecm0 slot extlinux.conf;extlinux/extlinux.conf"
IMAGE_BOOT_FILES:append = " nanokvm-initramfs-image-${MACHINE}.cpio.gz;initramfs"
do_image_wic[depends] += "nanokvm-gadget:do_deploy"

# --- Publish under the original LicheeRV-Nano-Build image name ---
# The upstream build emitted ${BOARD_SHORT}-${VARIANT}_${STORAGE_TYPE}.img.xz =
# "licheervnano-kvm_sd.img.xz". The wic output is the byte-identical raw SD image
# (only the Yocto .wic extension differs), so also expose it under that name:
# a real standalone copy of the compressed image, plus a symlink for the raw one.
NANOKVM_IMG_ALIAS = "licheervnano-kvm_sd"
# do_image_complete is a python task, so register a shell postfunc rather than
# appending shell to it. Runs before the task's sstate capture, so the aliases
# are deployed to DEPLOY_DIR_IMAGE alongside the .wic artifacts.
create_nanokvm_img_alias() {
    if [ -e "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic.xz" ]; then
        cp -fL "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic.xz" \
               "${IMGDEPLOYDIR}/${NANOKVM_IMG_ALIAS}.img.xz"
    fi
    if [ -e "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic" ]; then
        ln -sf "${IMAGE_LINK_NAME}.wic" "${IMGDEPLOYDIR}/${NANOKVM_IMG_ALIAS}.img"
    fi
}
do_image_complete[postfuncs] += "create_nanokvm_img_alias"
