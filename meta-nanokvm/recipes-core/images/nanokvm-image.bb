SUMMARY = "NanoKVM SD card image — IP KVM system for LicheeRV Nano / SG2002"
LICENSE = "MIT"

inherit core-image

# --- Root filesystem: squashfs + volatile overlay -------------------------
# The root is an immutable squashfs-zst image in an A/B slot pair (p2/p3);
# the initramfs (nanokvm-initramfs-image) lays a volatile tmpfs overlay over
# it and mounts the ext4 data partition at /var/lib/nanokvm. Everything that
# must survive a reboot lives there — see the initramfs /init for the
# persistence contract. A hard power cut can therefore never corrupt the root:
# every boot starts from the exact image bytes.
#
# The same holds for the boot payload now: /boot is mounted read-only, the FIT
# is hash-verified, and the only writable boot state is U-Boot's raw redundant
# environment. The whole startup path is effectively ephemeral, with the
# writable overlay attached only once the kernel is up.
IMAGE_FSTYPES += "squashfs-zst"
IMAGE_TYPEDEP:wic = "squashfs-zst"
WKS_FILE = "nanokvm-sd.wks.in"

# wic consumes boot.scr and boot_a.itb/boot_b.itb (IMAGE_BOOT_FILES) and
# uboot-env.bin (the raw env sectors), so every producer must have deployed
# before do_image_wic.
do_image_wic[depends] += "nanokvm-boot-fit:do_deploy nanokvm-uboot-env:do_deploy"
do_image_wic[depends] += "nanokvm-boot-script:do_deploy"

# Use the plain packagegroup-base (not -extended): -extended unconditionally
# RDEPENDS packagegroup-base-wifi -> wireless-regdb-static and would drag in
# wireless kernel modules. WiFi is intentionally absent from this image (the
# board carries no radio part), so packagegroup-base pulls the wifi subgroup
# only when MACHINE_FEATURES advertises it -- it does not.
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
# Intentionally none. There is no radio part on the board, and CONFIG_WLAN /
# CONFIG_WIRELESS / CONFIG_BT are off in nanokvm.cfg.

# --- Boot health / A/B -----------------------------------------------------
# The userspace half of the RAUC A/B flow. nanokvm-bootok restores the running
# slot's BOOT_<slot>_LEFT once the slot has proven it boots -- mandatory, not
# optional: the bootmeth decrements that counter on every boot, so without this
# a healthy system counts itself down and switches slots for no reason.
# nanokvm-update installs an image into the inactive slot and activates it, and
# nanokvm-growdata does the partition growth that used to run in the initramfs.
IMAGE_INSTALL:append = " \
    nanokvm-boot-health \
    "

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
# i2c-eeprom is gone, and so is the bus it served: the i2c1 slave EEPROM that
# presented a 24c512 to the managed host has no consumer any more, so the board
# DTS no longer declares it and the kernel no longer builds slave-mode i2c
# (board DTS patch 0003, nanokvm.cfg). nanokvm-gadget is deploy-only now
# (boot-partition files via do_image_wic below) -- its first-boot seeding
# script moved into the server.
IMAGE_INSTALL:append = " \
    nanokvm-server \
    "

# --- Local HDMI capture (CVITek multimedia stack) ---
# The out-of-tree modules that drive VI -> VPSS -> VENC on the SG2002,
# forward-ported to 6.18 (meta-sophgo/recipes-kernel/soph-media), plus the
# soph_v4l2 media-controller front-end that exposes them as /dev/media0 +
# /dev/video0. They are installed but deliberately NOT auto-loaded: load
# order is a real dependency chain (sys, base, snsr_i2c, cif, vi, vpss,
# vcodec, jpeg, cvi_vc_drv, v4l2) and the service that owns capture brings
# the pipeline up, so there is no modules-load drop-in and no modprobe
# alias. ION is not here -- it is built into the kernel (linux-sophgo
# bbappend patch 0009), because it calls symbols mainline does not export to
# modules.
#
# The kernel-module-* packages are the in-tree V4L2 core soph_v4l2 links
# against (videodev, mc, videobuf2*) -- kernel modules are never installed
# implicitly, and without them the front-end cannot load. Listed one by one
# rather than pulling the kernel-modules meta package, which would drag in
# every =m symbol the fragment ever creates (vimc among them, which is a
# kconfig anchor, not a runtime dependency -- see nanokvm.cfg).
IMAGE_INSTALL:append = " \
    soph-media \
    kernel-module-videodev \
    kernel-module-mc \
    kernel-module-videobuf2-common \
    kernel-module-videobuf2-v4l2 \
    kernel-module-videobuf2-memops \
    kernel-module-videobuf2-vmalloc \
    "

# --- No host firmware image ------------------------------------------------
# The BMC used to carry a Raspberry Pi boot image on the data partition and
# serve it to the managed host over the USB mass-storage gadget, staged at
# build time by rpi-firmware-seed from a second "rpi" multiconfig. All of that
# is gone -- the layer, the multiconfig, the seed recipe and the pre-populated
# partition. Host firmware is updated with UEFI FMP capsules instead, which
# the host's own EDK2 picks up from \EFI\UpdateCapsule\ on the mass-storage
# gadget, so the BMC has no reason to hold a boot image at all.
#
# The consequences worth noting: the data partition now ships EMPTY (no
# build-time image surgery, no 500 MB of factory content in the .wic), and a
# `kas build` no longer drags an entire aarch64 TF-A/U-Boot/RPi-firmware
# toolchain and a crane-based DTB fetch along behind it.

# --- SD card image via WKS ---
# (No IMAGE_ROOTFS_SIZE: squashfs is content-sized; the wks pins the slot
# partitions at 512 MB and rawcopy errors out if the squashfs outgrows them.)
do_image_wic[depends] += "fsbl:do_deploy linux-sophgo:do_deploy nanokvm-initramfs-image:do_image_complete"

# --- Boot-partition config files ---
# nanokvm-gadget deploys these to DEPLOY_DIR_IMAGE; wic's bootimg-partition puts
# them on the FAT boot partition next to fip.bin and the A/B FIT payloads. Read
# at runtime from the read-only /boot mount by the NanoKVM server
# (server/service/usbgadget owns the gadget now; usb.ecm0 seeds the default ECM
# function on first boot).
#
# Gone from this list: "extlinux.conf" (the boot menu -- boot.scr replaced it,
# deployed by nanokvm-boot-script), "slot" (the rootfs selector -- the RAUC
# bootmeth keeps it in the U-Boot environment, so nothing has to write to the
# boot partition to change slots) and the standalone initramfs (packed into
# the FIT).
IMAGE_BOOT_FILES:append = " board hostname.prefix ver usb.ecm0"
do_image_wic[depends] += "nanokvm-gadget:do_deploy"

# --- A/B update bundle -----------------------------------------------------
# What nanokvm-update consumes: the two halves of a slot, together. They have
# to ship as one artifact because they have to be installed as one -- the FIT
# carries the initramfs that mounts the squashfs, and installing either without
# the other leaves a slot that boots the wrong pair.
#
# Plain tar + gzip, because the image has busybox tar and gzip and no xz, and
# the payload is a zstd squashfs already. Reproducible: sorted names, epoch
# mtime, numeric owner, gzip -n.
# do_update_bundle runs as a postfunc, not a task, so its dependency has to
# hang on the task that actually runs it.
do_image_complete[depends] += "nanokvm-boot-fit:do_deploy"
do_update_bundle() {
    stage="${WORKDIR}/update-bundle"
    rm -rf "$stage"
    mkdir -p "$stage"
    squashfs="${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.squashfs-zst"
    itb="${DEPLOY_DIR_IMAGE}/boot_a.itb"

    if [ ! -e "$squashfs" ]; then
        bbwarn "update bundle: $squashfs missing, skipping"
        return
    fi
    [ -e "$itb" ] || bbfatal "update bundle: $itb missing (nanokvm-boot-fit should have deployed it)"

    # boot_a.itb and boot_b.itb are the same bytes at build time; the slot is
    # chosen at install time by nanokvm-update, so the bundle carries one.
    cp -L "$squashfs" "$stage/rootfs.squashfs"
    cp -L "$itb"      "$stage/boot.itb"
    ( cd "$stage" && sha256sum rootfs.squashfs boot.itb > sha256sums )

    tar --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime="@${SOURCE_DATE_EPOCH}" \
        -cf "$stage/bundle.tar" -C "$stage" boot.itb rootfs.squashfs sha256sums
    gzip -n -f "$stage/bundle.tar"

    install -m 0644 "$stage/bundle.tar.gz" "${IMGDEPLOYDIR}/${IMAGE_NAME}.update.tar.gz"
    ln -sf "${IMAGE_NAME}.update.tar.gz" "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.update.tar.gz"
    bbplain "update bundle: $(stat -c %s "$stage/bundle.tar.gz") B -> ${IMAGE_LINK_NAME}.update.tar.gz"
}
# After do_image_complete so the squashfs is in IMGDEPLOYDIR, and registered as
# one of its postfuncs' peers rather than a separate sstate task: the bundle
# lands in IMGDEPLOYDIR, which do_image_complete captures.
do_image_complete[postfuncs] += "do_update_bundle"

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
