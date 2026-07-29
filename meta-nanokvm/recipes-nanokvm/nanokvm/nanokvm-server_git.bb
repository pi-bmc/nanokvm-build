SUMMARY = "NanoKVM BMC server (pi-bmc/nanokvm-app) for LicheeRV Nano / SG2002"
HOMEPAGE = "https://github.com/pi-bmc/nanokvm-app"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://src/${GO_IMPORT}/LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

inherit go-mod go update-rc.d

# NB: the GitHub repo is pi-bmc/nanokvm-app, but the Go module path (go.mod) is
# github.com/BMCPi/NanoKVM -- GO_IMPORT must match the module path, not the URL.
GO_IMPORT = "github.com/BMCPi/NanoKVM"
SRCREV = "${AUTOREV}"
SRC_URI = "git://github.com/pi-bmc/nanokvm-app;branch=main;protocol=https \
           file://nanokvm.init"

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

    # Only the server is built/installed. The repo's other cmd/ tools
    # (rpiboot, fw_env) are developer CLIs the server never execs -- it does
    # rpiboot-mode entry in-process (server/service/power) and owns the
    # firmware image env itself (server/service/firmware) -- so they have no
    # place in the image.
    ${GO} build -v -trimpath -modcacherw -ldflags "-s -w" \
        -o ${B}/NanoKVM-Server ./cmd/server

    # Keep the module cache (incl. the read-only downloaded toolchain) writable
    # so bitbake/rm can clean ${B} later.
    chmod -R u+w ${B}/.mod 2>/dev/null || true
}

# /kvmapp is the FACTORY copy of the app, baked into the read-only squashfs.
# Self-updates never touch it: they install to /var/lib/nanokvm/app on the
# persistent data partition, and the init script launches the first runnable
# of app, app.prev, /kvmapp — so /kvmapp is the always-bootable fallback.
KVMAPP_DIR = "/kvmapp"

do_install() {
    install -d ${D}${KVMAPP_DIR}/server
    install -m 0755 ${B}/NanoKVM-Server ${D}${KVMAPP_DIR}/server/NanoKVM-Server

    # Service init script (start/stop/status via start-stop-daemon), registered
    # through oe-core sysvinit/update-rc.d. We ship our own instead of the app's
    # packaging/etc/init.d/S95nanokvm because the upstream one uses Debian-only
    # start-stop-daemon options (-O logfile, -d chdir) that BusyBox rejects, so
    # the daemon never launches on this image. See files/nanokvm.init.
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nanokvm.init \
        ${D}${sysconfdir}/init.d/nanokvm

    # Compat name. The server restarts itself by shelling out to the literal
    # path "/etc/init.d/S95nanokvm restart" -- server/service/vm/tls.go,
    # service/application/update.go (x2) and update_offline.go. update-rc.d
    # names the script "nanokvm" and puts the S95 prefix only on the rc?.d
    # symlinks, so that path did not exist and every self-restart (after a TLS
    # cert change or an app upgrade) silently did nothing.
    ln -sf nanokvm ${D}${sysconfdir}/init.d/S95nanokvm
}

INITSCRIPT_NAME = "nanokvm"
INITSCRIPT_PARAMS = "defaults 95"

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
    ${sysconfdir}/init.d/nanokvm \
    ${sysconfdir}/init.d/S95nanokvm \
    "
