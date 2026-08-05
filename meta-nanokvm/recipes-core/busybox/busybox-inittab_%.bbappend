# Replace poky's stock busybox inittab with this image's minimal one: the
# initramfs already owns the proc/sys/devtmpfs mounts and the overlay root is
# always writable, so the stock mount/remount preamble is dead weight (and
# prints "already mounted" errors on every boot). See files/inittab.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
