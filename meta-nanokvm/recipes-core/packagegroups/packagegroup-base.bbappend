# packagegroup-base hard-RDEPENDS on module-init-tools (kmod) unconditionally.
# This kernel is CONFIG_MODULES=n -- there is nothing to load, and the image
# comment in nanokvm-image.bb has claimed kmod dropped since the module-less
# kernel landed. Remove the dep so the claim is true (busybox's equally inert
# insmod/modprobe applets remain). liblzma5 leaves with it.
RDEPENDS:${PN}:remove = "module-init-tools"
