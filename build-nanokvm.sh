#!/bin/bash -e
#
# NanoKVM-only Buildroot build.
#
# This replaces the previous sed-driven build-nanokvm.sh. Instead of mutating
# vendor files in place at build time, all NanoKVM customisation now lives as
# version-controlled files under nanokvm/ and is *installed by copy* before the
# build. See nanokvm/README.md for provenance of each file.
#
#   nanokvm/buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig
#       Final Buildroot defconfig (base + nanokvm + maix-cdk + shrink deltas
#       pre-applied). Replaces ~90 lines of `sed` on configs/.
#   nanokvm/build/.../memmap.py
#       RAM split (ION 75M, bootlogo 5632K). Replaces the memmap.py sed.
#   nanokvm/build/.../genimage_rootless.cfg
#   nanokvm/build/.../sd_gen_burn_image_rootless.sh
#       SD image layout + USB gadget set (disk/touchpad/mouse/keyboard/rndis),
#       MaixCAM board file, hostname prefix. Replaces the genimage seds.
#   nanokvm/overlay/etc/init.d/S99ipmi_sim
#       Replaces patches/buildroot.patch.
#
# The fixed feature set is: nanokvm=y maix-cdk=y shrink=y (tailscale/tpu/oss off).

export SG_BOARD_FAMILY=${SG_BOARD_FAMILY:-sg200x}
export SG_BOARD_LINK=${SG_BOARD_LINK:-sg2002_licheervnano_sd}

TOP=$(cd "$(dirname "$0")" && pwd)
SRC="${TOP}/nanokvm"
BR_OUTPUT_DIR=output

# Keep sbin dirs on PATH (vendor build expects mkfs/parted/etc.).
for p in / /usr/ /usr/local/ ; do
  if echo "$PATH" | grep -q ${p}bin ; then
    if ! echo "$PATH" | grep -q ${p}sbin ; then
      export PATH=${p}sbin:$PATH
    fi
  fi
done

# ---------------------------------------------------------------------------
# Install NanoKVM overrides into the vendor submodules (no sed, idempotent).
# ---------------------------------------------------------------------------
install_overrides() {
  install -m 0644 "${SRC}/build/boards/${SG_BOARD_FAMILY}/${SG_BOARD_LINK}/memmap.py" \
    "build/boards/${SG_BOARD_FAMILY}/${SG_BOARD_LINK}/memmap.py"
  install -m 0644 "${SRC}/build/tools/common/sd_tools/genimage_rootless.cfg" \
    "build/tools/common/sd_tools/genimage_rootless.cfg"
  install -m 0755 "${SRC}/build/tools/common/sd_tools/sd_gen_burn_image_rootless.sh" \
    "build/tools/common/sd_tools/sd_gen_burn_image_rootless.sh"
  install -m 0644 "${SRC}/buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig" \
    "buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig"
  # Rootfs overlay additions (e.g. S99ipmi_sim) — copied into the overlay the
  # base defconfig already points BR2_ROOTFS_OVERLAY at.
  cp -a "${SRC}/overlay/." "buildroot/board/cvitek/SG200X/overlay/"
}

# Restore the vendor submodules to a pristine working tree.
restore_submodules() {
  git -C build checkout -- \
    "boards/${SG_BOARD_FAMILY}/${SG_BOARD_LINK}/memmap.py" \
    tools/common/sd_tools/genimage_rootless.cfg \
    tools/common/sd_tools/sd_gen_burn_image_rootless.sh 2>/dev/null || true
  git -C buildroot checkout -- \
    configs/cvitek_SG200X_musl_riscv64_defconfig \
    board/cvitek/SG200X/overlay 2>/dev/null || true
  git -C buildroot clean -fdq board/cvitek/SG200X/overlay 2>/dev/null || true
}
trap restore_submodules EXIT

# ---------------------------------------------------------------------------
# Prepare sources (toolchains, kernel/u-boot dts symlinks).
# ---------------------------------------------------------------------------
if [ -e prepare-licheesgnano.sh ]; then
  . ./prepare-licheesgnano.sh
fi

install_overrides

# ---------------------------------------------------------------------------
# Buildroot overlay init.d shaping for NanoKVM. These steps act on generated /
# per-package artifacts and so remain imperative (they are not expressible as
# static config), but the multi-board / multi-feature branching is gone.
# ---------------------------------------------------------------------------
OVR=buildroot/board/cvitek/SG200X/overlay/etc/init.d

# NanoKVM serves UVC itself; drop the prebuilt gadget server blobs.
rm -f "${OVR}/uvc-gadget-server.elf" "${OVR}/uvc-gadget-server.tar.xz"

# NanoKVM only uses S00kmod, S01fs and S03usbdev.
for f in S07fs2 S07kmod2 S08usbdev ; do
  rm -f "${OVR}/${f}" "buildroot/${BR_OUTPUT_DIR}/target/etc/init.d/${f}"
done

# On incremental builds, pull NanoKVM's own init scripts from the package output
# and drop the ones managed elsewhere.
PP=buildroot/${BR_OUTPUT_DIR}/per-package/nanokvm-sg200x/target/kvmapp/system/init.d
if [ -e "${PP}" ]; then
  rsync -r --copy-dirlinks --copy-links --hard-links "${PP}/" "${OVR}/"
  rm -f "${OVR}"/S*kvm* "${OVR}"/S*tailscale* "${OVR}"/S*usbhid* "${OVR}"/S*usbkeyboard*
fi

# Gadget NIC service is named S30rndis for NanoKVM.
if [ -e "${OVR}/S30gadget_nic" ] && [ ! -e "${OVR}/S30rndis" ]; then
  mv "${OVR}/S30gadget_nic" "${OVR}/S30rndis"
fi

# ---------------------------------------------------------------------------
# Build.
# ---------------------------------------------------------------------------
source build/cvisetup.sh
defconfig "${SG_BOARD_LINK}"
build_all

# ---------------------------------------------------------------------------
# Package NanoKVM-Server payload.
# ---------------------------------------------------------------------------
installdir="${TOP}/install/soc_${SG_BOARD_LINK}"
target="buildroot/${BR_OUTPUT_DIR}/target"
if [ -e "${target}/kvmapp/server/NanoKVM-Server" ]; then
  mkdir -p "${installdir}"
  rm -f "${installdir}/nanokvm-latest.zip"
  ( cd "${target}" && ln -sf kvmapp latest \
      && zip -r --symlinks "${installdir}/nanokvm-latest.zip" latest/* \
      && rm -f latest )
fi

# Strip leftovers that must not ship in the rootfs.
rm -f "${target}/etc/tailscale_disabled"
rm -f "${target}"/etc/init.d/S*kvm* "${target}"/etc/init.d/S*tailscale* \
      "${target}"/etc/init.d/S*usbdev* "${target}"/etc/init.d/S*usbhid* \
      "${target}"/etc/init.d/S*usbkeyboard*
rm -f "${target}/usr/bin/tailscale" "${target}/usr/sbin/tailscaled"
rm -rf "${target}/kvmapp/"

echo OK
