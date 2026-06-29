#!/usr/bin/env bash
# Set up the Yocto build environment for NanoKVM / LicheeRV Nano.
#
# Option A — kas (recommended, handles repo fetching automatically):
#   pip3 install kas
#   kas build kas.yml
#
# Option B — manual (this script):
#   ./setup-yocto.sh
#   cd build-yocto && source env.sh && bitbake nanokvm-image
#
# The script clones Poky and all required meta-layers alongside this repo,
# then writes build-yocto/conf/{bblayers,local}.conf.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${REPO_ROOT}/build-yocto"
LAYERS_DIR="${REPO_ROOT}/yocto-layers"

POKY_URL="https://git.yoctoproject.org/poky"
META_OE_URL="https://git.openembedded.org/meta-openembedded"
META_RISCV_URL="https://github.com/riscv/meta-riscv"
BRANCH="scarthgap"

clone_or_update() {
    local url="$1" dest="$2" branch="${3:-${BRANCH}}"
    if [ -d "${dest}/.git" ]; then
        echo "  Updating ${dest##*/}…"
        git -C "${dest}" fetch origin
        git -C "${dest}" checkout "origin/${branch}" --detach -q
    else
        echo "  Cloning ${url##*/}…"
        git clone --depth=1 --branch "${branch}" "${url}" "${dest}"
    fi
}

echo "==> Fetching Yocto layers into ${LAYERS_DIR}"
mkdir -p "${LAYERS_DIR}"
clone_or_update "${POKY_URL}"     "${LAYERS_DIR}/poky"
clone_or_update "${META_OE_URL}"  "${LAYERS_DIR}/meta-openembedded"
clone_or_update "${META_RISCV_URL}" "${LAYERS_DIR}/meta-riscv"

echo "==> Configuring build directory: ${BUILD_DIR}"
mkdir -p "${BUILD_DIR}/conf"

# Write conf files directly — oe-init-build-env must be sourced interactively
# by the user; attempting to source it in a subshell hits "BBSERVER: unbound variable".
cat > "${BUILD_DIR}/conf/bblayers.conf" <<BBLAYERS
BBLAYERS ?= " \\
    ${LAYERS_DIR}/poky/meta \\
    ${LAYERS_DIR}/poky/meta-poky \\
    ${LAYERS_DIR}/meta-openembedded/meta-oe \\
    ${LAYERS_DIR}/meta-openembedded/meta-networking \\
    ${LAYERS_DIR}/meta-openembedded/meta-python \\
    ${LAYERS_DIR}/meta-openembedded/meta-filesystems \\
    ${LAYERS_DIR}/meta-riscv \\
    ${REPO_ROOT}/meta-sophgo \\
    ${REPO_ROOT}/meta-sophgo-sdk \\
    ${REPO_ROOT}/meta-nanokvm \\
    "
BBLAYERS

cat > "${BUILD_DIR}/conf/local.conf" <<LOCALCONF
MACHINE = "sg2002-licheervnano"
DISTRO = "nanokvm"
TCLIBC = "musl"

BB_NUMBER_THREADS = "\${@oe.utils.cpu_count()}"
PARALLEL_MAKE = "-j \${@oe.utils.cpu_count()}"

DL_DIR = "${REPO_ROOT}/downloads"
SSTATE_DIR = "${REPO_ROOT}/sstate-cache"

LICENSE_FLAGS_ACCEPTED = "commercial"
PACKAGE_CLASSES = "package_ipk"
LOCALCONF

# Write a convenience wrapper so you can just run `. build-yocto/env.sh`
cat > "${BUILD_DIR}/env.sh" <<'ENVSH'
# Source this to enter the Yocto build environment.
# Usage: source build-yocto/env.sh
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAYERS_DIR="$(dirname "${SCRIPT_DIR}")/yocto-layers"
source "${LAYERS_DIR}/poky/oe-init-build-env" "${SCRIPT_DIR}"
ENVSH

chmod +x "${BUILD_DIR}/env.sh"

echo ""
echo "==> Done. To build:"
echo ""
echo "    source build-yocto/env.sh"
echo "    bitbake nanokvm-image"
echo ""
echo "Or with kas (fetches layers automatically):"
echo ""
echo "    pip3 install kas"
echo "    kas build kas.yml"
