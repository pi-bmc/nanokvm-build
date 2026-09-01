SUMMARY = "NanoKVM BMC server (pi-bmc/nanokvm-app) for LicheeRV Nano / SG2002"
HOMEPAGE = "https://github.com/pi-bmc/nanokvm-app"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://src/${GO_IMPORT}/LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

inherit go-mod go

# NB: the GitHub repo is pi-bmc/nanokvm-app, but the Go module path (go.mod) is
# github.com/pi-bmc/nanokvm-app -- GO_IMPORT must match the module path, not the URL.
GO_IMPORT = "github.com/pi-bmc/nanokvm-app"
SRCREV = "${AUTOREV}"
SRC_URI = "git://github.com/pi-bmc/nanokvm-app;branch=edk2;protocol=https \
           file://nanokvm-server-run"

S = "${WORKDIR}/git"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# The refactored app is pure Go (CGO disabled) -- no cvitek vision libs, no cgo,
# no patchelf. The generated code (templ *_templ.go and the tailwind
# server/assets/css/output.css) is committed upstream, so no templ/tailwindcss
# codegen tooling is needed at build time; a plain `go build ./cmd/server`
# suffices. (Upstream can alternatively build via goreleaser, but that needs
# Docker + the goreleaser tool, so we drive `go build` directly here.)

# go.mod requires Go 1.25 while oe-core ships an older Go; GOTOOLCHAIN=auto lets
# the toolchain fetch the required version (needs network during compile). Go
# module dependencies are also fetched at build time (no vendor/ dir upstream).
do_compile[network] = "1"

# Module/build root (go.bbclass unpacks the checkout under src/${GO_IMPORT} and
# sets GOPATH=${B}).
GO_APP_DIR = "${B}/src/${GO_IMPORT}"

# go-mod adds "${B}/.mod" to cleandirs, but the auto-downloaded toolchain marks
# its dirs read-only so that rm fails; drop .mod from cleandirs and make it
# writable at the end instead.
do_compile[cleandirs] = "${B}/bin ${B}/pkg"

do_compile() {
    cd ${GO_APP_DIR} || bbfatal "go module dir ${GO_APP_DIR} not found"

    # Mirror the Makefile's canonical build. Its `app` target additionally runs
    # `generate` (templ + the local tailwindcss tool) and `format`
    # (golangci-lint), which need extra tooling/Docker -- but the generated code
    # (server/templates/*_templ.go, server/assets/css/output.css) is committed
    # upstream and go:embed'd, so the underlying `dist/*/...` go-build targets
    # alone reproduce the binaries:
    #   CGO_ENABLED=0 GOOS=linux GOARCH=riscv64 go build -o <out> ./cmd/<x>
    # GOOS/GOARCH come from go.bbclass; CGO is off (pure Go, no cvitek libs).
    export CGO_ENABLED="0"
    # go.mod requires Go 1.25.x but poky ships 1.22; auto-fetch the toolchain.
    export GOTOOLCHAIN="auto"
    export GOSUMDB="sum.golang.org"
    # -mod=mod tolerates an out-of-date go.sum; -modcacherw keeps the (otherwise
    # read-only) module/toolchain cache writable so cleandirs/rm can remove it.
    export GOFLAGS="-mod=mod"

    # Only the server is built/installed. The repo's other cmd/ tools are
    # developer CLIs the server never execs -- it does host-boot-mode entry
    # in-process (server/service/power) -- so they have no place in the image.
    # (fw_setenv, which the A/B flow does use, comes from libubootenv-bin.)
    ${GO} build -v -trimpath -modcacherw -ldflags "-s -w" \
        -o ${B}/NanoKVM-Server ./cmd/server

    # Keep the module cache (incl. the read-only downloaded toolchain) writable
    # so bitbake/rm can clean ${B} later.
    chmod -R u+w ${B}/.mod 2>/dev/null || true
}

# Compress the binary the way the upstream Makefile's dist/server target does
# (`upx -q -v ./dist/server/NanoKVM-Server`), from a native UPX rather than a
# host tool so the build stays hermetic.
#
# NOTE (measured on the 2026-08 tree, 43.5 MB server binary): this SHRINKS the
# binary to 14.4 MB but GROWS the squashfs, because IMAGE_FSTYPES is
# squashfs-zst and zstd compresses the plain binary better than UPX does
# (11.8 MB) while the packed one is near-incompressible (13.7 MB) -- about
# 1.9 MB of image for nothing. It also costs RAM at runtime: exec'ing a packed
# binary decompresses all 43.5 MB into anonymous pages that cannot be evicted,
# where the unpacked one is demand-paged from the squashfs. Where UPX does pay
# off is the OTA path the Makefile targets -- self-updates install to
# /var/lib/nanokvm/app on the ext4 data partition, uncompressed, and keep an
# app.prev alongside. Set UPX_COMPRESS = "0" to leave /kvmapp's copy plain.
UPX_COMPRESS ?= "1"

DEPENDS += "${@bb.utils.contains('UPX_COMPRESS', '1', 'upx-native', '', d)}"

do_compile:append() {
    if [ "${UPX_COMPRESS}" = "1" ]; then
        # -q -v matches the Makefile: one summary line into log.do_compile.
        upx -q -v ${B}/NanoKVM-Server
    fi
}

# UPX rewrites the ELF into a self-extracting image, so the packaging strip
# pass must not touch it -- `strip` on a packed binary corrupts it. There is
# nothing to strip in any case: Go links with -s -w and does its own stripping,
# which is what the already-stripped waiver below is for.
INHIBIT_PACKAGE_STRIP = "${UPX_COMPRESS}"
INHIBIT_PACKAGE_DEBUG_SPLIT = "${UPX_COMPRESS}"

# /kvmapp is the FACTORY copy of the app, baked into the read-only squashfs.
# Self-updates never touch it: they install to /var/lib/nanokvm/app on the
# persistent data partition, and the init script launches the first runnable
# of app, app.prev, /kvmapp — so /kvmapp is the always-bootable fallback.
# On a board where neither app nor app.prev is populated the launcher first
# copies this binary into /var/lib/nanokvm/app and runs it from there, so the
# supervised process normally lives on the writable path rather than on the
# squashfs; /kvmapp is only exec'd directly when that copy cannot be made.
KVMAPP_DIR = "/kvmapp"

do_install() {
    install -d ${D}${KVMAPP_DIR}/server
    install -m 0755 ${B}/NanoKVM-Server ${D}${KVMAPP_DIR}/server/NanoKVM-Server

    # The launcher busybox init runs under its inittab ::respawn entry (see
    # the busybox inittab in recipes-core/busybox/files). It seeds
    # /var/lib/nanokvm/app from /kvmapp when empty, then walks the
    # app -> app.prev -> /kvmapp cascade on every (re)start and throttles
    # crash loops; the server restarts itself by simply exiting
    # (server/service/application RestartService). This replaced the sysv
    # init script + start-stop-daemon + pidfile machinery -- supervision now
    # lives where init already supervises the getty.
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/nanokvm-server-run ${D}${sbindir}/nanokvm-server-run
}

# The server's network service (server/service/network) shells out to `nft`
# for the usb0 forward-path guard; it degrades gracefully when the binary is
# absent, so a recommendation rather than a hard dependency. Everything else
# networking (DHCP client/server, netlink addressing, mDNS) is in-process.
RRECOMMENDS:${PN} = "nftables"

# Statically-linked Go binaries: Go does its own linking/stripping, so skip the
# LDFLAGS-injection and already-stripped QA checks that don't apply.
INSANE_SKIP:${PN} += "ldflags already-stripped"

FILES:${PN} = " \
    ${KVMAPP_DIR} \
    ${sbindir}/nanokvm-server-run \
    "
