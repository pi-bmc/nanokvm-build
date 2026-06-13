#!/bin/bash -e

export SG_BOARD_FAMILY=sg200x
export SG_BOARD_LINK=sg2002_licheervnano_sd

sdkver=keep
kernel_entry_addr=0x80200000
loadaddr=0x80200000
ramdisk_addr_r=0x83000000
fdt_addr_r=0x88000000
keep_tmp=n

while [ "$#" -gt 0 ]; do
  case "$1" in
    --board=*|--board-link=*)
      export SG_BOARD_LINK=$(echo "$1" | cut -d '=' -f 2-)
      shift
      ;;
    --sdk-ver=*|--sdkver=*)
      sdkver=$(echo "$1" | cut -d '=' -f 2-)
      shift
      ;;
    --kernel-entry=*)
      kernel_entry_addr=$(echo "$1" | cut -d '=' -f 2-)
      shift
      ;;
    --loadaddr=*)
      loadaddr=$(echo "$1" | cut -d '=' -f 2-)
      shift
      ;;
    --ramdisk-addr=*)
      ramdisk_addr_r=$(echo "$1" | cut -d '=' -f 2-)
      shift
      ;;
    --fdt-addr=*)
      fdt_addr_r=$(echo "$1" | cut -d '=' -f 2-)
      shift
      ;;
    --keep-tmp)
      keep_tmp=y
      shift
      ;;
    -h|--help)
      cat <<'EOF'
Usage: ./build-uroot.sh [options]

Options:
  --board=<name>           Board link name (default: sg2002_licheervnano_sd)
  --sdk-ver=<name>         SDK/toolchain variant passed to prepare script
  --kernel-entry=<addr>    Kernel load/entry address for uImage (default: 0x80200000)
  --loadaddr=<addr>        U-Boot kernel load address used in boot cmd file
  --ramdisk-addr=<addr>    U-Boot initrd load address used in boot cmd file
  --fdt-addr=<addr>        U-Boot DTB load address used in boot cmd file
  --keep-tmp               Keep temporary u-root work dir

Requires:
  - Go toolchain on host
  - Network access on first run (go module download)
EOF
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

for p in / /usr/ /usr/local/ ; do
  if echo "$PATH" | grep -q "${p}bin" ; then
    if ! echo "$PATH" | grep -q "${p}sbin" ; then
      export PATH=${p}sbin:$PATH
    fi
  fi
done

if echo "${SG_BOARD_LINK}" | grep -q -E '^cv180' ; then
  export SG_BOARD_FAMILY=cv180x
fi
if echo "${SG_BOARD_LINK}" | grep -q -E '^sg200' ; then
  export SG_BOARD_FAMILY=sg200x
fi

if [ -e prepare-licheesgnano.sh ]; then
  export sdkver
  . ./prepare-licheesgnano.sh
fi

source build/cvisetup.sh
defconfig "${SG_BOARD_LINK}"

if ! command -v go >/dev/null 2>&1 ; then
  echo "error: go is required to build u-root" >&2
  exit 1
fi

echo "==> Building u-boot and kernel"
build_uboot
build_kernel noitb

uroot_arch=riscv64
case "${CONFIG_ARCH}" in
  arm) uroot_arch=arm ;;
  arm64) uroot_arch=arm64 ;;
  riscv) uroot_arch=riscv64 ;;
  riscv64) uroot_arch=riscv64 ;;
esac

outdir=$(pwd)/install/soc_${SG_BOARD_LINK}/uroot
tmpdir=$(mktemp -d)
mkdir -p "${outdir}"

echo "==> Building minimal u-root initramfs (${uroot_arch})"
GOOS=linux GOARCH="${uroot_arch}" CGO_ENABLED=0 \
  go run github.com/u-root/u-root/cmd/u-root@latest \
    -format=cpio \
    -o "${tmpdir}/uroot.cpio.gz" \
    github.com/u-root/u-root/cmds/core/init \
    github.com/u-root/u-root/cmds/core/sh \
    github.com/u-root/u-root/cmds/core/ls \
    github.com/u-root/u-root/cmds/core/cat \
    github.com/u-root/u-root/cmds/core/dmesg \
    github.com/u-root/u-root/cmds/core/ip \
    github.com/u-root/u-root/cmds/core/mount \
    github.com/u-root/u-root/cmds/core/echo

kernel_img="${RAMDISK_PATH}/${RAMDISK_OUTPUT_FOLDER}/Image"
if [ ! -e "${kernel_img}" ]; then
  echo "error: kernel image not found: ${kernel_img}" >&2
  exit 1
fi

mkimage_tool="${UBOOT_PATH}/${UBOOT_OUTPUT_FOLDER}/tools/mkimage"
if [ ! -x "${mkimage_tool}" ]; then
  echo "error: mkimage not found: ${mkimage_tool}" >&2
  exit 1
fi

dtb_img=$(find "${RAMDISK_PATH}/${RAMDISK_OUTPUT_FOLDER}" -maxdepth 1 -name "*${SG_BOARD_LINK}*.dtb" | head -n 1)
if [ -z "${dtb_img}" ]; then
  dtb_img=$(find "${RAMDISK_PATH}/${RAMDISK_OUTPUT_FOLDER}" -maxdepth 1 -name "*.dtb" | head -n 1)
fi
if [ -z "${dtb_img}" ]; then
  echo "error: no dtb found in ${RAMDISK_PATH}/${RAMDISK_OUTPUT_FOLDER}" >&2
  exit 1
fi

echo "==> Creating u-boot loadable artifacts"
"${mkimage_tool}" -A "${CONFIG_ARCH}" -O linux -T kernel -C none \
  -a "${kernel_entry_addr}" -e "${kernel_entry_addr}" \
  -n "Linux ${SG_BOARD_LINK}" -d "${kernel_img}" "${outdir}/Image.uImage"

"${mkimage_tool}" -A "${CONFIG_ARCH}" -O linux -T ramdisk -C gzip \
  -n "u-root initramfs" -d "${tmpdir}/uroot.cpio.gz" "${outdir}/uroot.uImage"

cp -f "${kernel_img}" "${outdir}/Image"
cp -f "${dtb_img}" "${outdir}/board.dtb"
cp -f "${tmpdir}/uroot.cpio.gz" "${outdir}/uroot.cpio.gz"

cat > "${outdir}/boot.cmd.txt" <<EOF
# Load from FAT partition and boot with u-root initramfs
fatload mmc 0:1 ${loadaddr} Image.uImage
fatload mmc 0:1 ${ramdisk_addr_r} uroot.uImage
fatload mmc 0:1 ${fdt_addr_r} board.dtb
bootm ${loadaddr} ${ramdisk_addr_r} ${fdt_addr_r}
EOF

if [ "${keep_tmp}" != y ]; then
  rm -rf "${tmpdir}"
fi

echo ""
echo "u-root artifacts generated in: ${outdir}"
echo "  - Image.uImage"
echo "  - uroot.uImage"
echo "  - board.dtb"
echo "  - boot.cmd.txt"
echo ""
echo "Copy files to your boot partition and run commands from boot.cmd.txt in U-Boot."
