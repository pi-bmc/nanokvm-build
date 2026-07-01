SUMMARY = "NanoKVM BMC server (pi-bmc/nanokvm-app) for LicheeRV Nano / SG2002"
HOMEPAGE = "https://github.com/pi-bmc/nanokvm-app"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://src/${GO_IMPORT}/LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

inherit go-mod go update-rc.d

# NB: the GitHub repo is pi-bmc/nanokvm-app, but the Go module path (go.mod) is
# github.com/BMCPi/NanoKVM -- GO_IMPORT must match the module path, not the URL.
GO_IMPORT = "github.com/BMCPi/NanoKVM"
SRCREV = "${AUTOREV}"
SRC_URI = "git://github.com/pi-bmc/nanokvm-app;branch=main;protocol=https"

S = "${WORKDIR}/git"

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
# sets GOPATH=${B}); the three main packages live in cmd/.
GO_APP_DIR = "${B}/src/${GO_IMPORT}"

# go-mod adds "${B}/.mod" to cleandirs, but the auto-downloaded toolchain marks
# its dirs read-only so that rm fails; drop .mod from cleandirs and make it
# writable at the end instead.
do_compile[cleandirs] = "${B}/bin ${B}/pkg"

do_compile() {
    cd ${GO_APP_DIR}

    export GOFLAGS="-mod=mod"
    export CGO_ENABLED="0"
    export GOTOOLCHAIN="auto"
    export GOSUMDB="sum.golang.org"
    # GOARCH/GOOS, GOPROXY and the module cache come from go.bbclass/go-mod.bbclass.

    # Stamp the version the way the goreleaser build does (main.version/commit/date).
    LDFLAGS="-s -w -X main.version=${PV} -X main.date=reproducible"

    ${GO} build -v -trimpath -ldflags "${LDFLAGS}" -o ${B}/NanoKVM-Server ./cmd/server
    ${GO} build -v -trimpath -ldflags "-s -w"       -o ${B}/rpiboot       ./cmd/rpiboot
    ${GO} build -v -trimpath -ldflags "-s -w"       -o ${B}/fw_env        ./cmd/fw_env

    # Keep the module cache (incl. the read-only downloaded toolchain) writable
    # so bitbake/rm can clean ${B} later.
    chmod -R u+w ${B}/.mod 2>/dev/null || true
}

# The app expects its server binary under /kvmapp/server (the init script stages
# it to /tmp/server at runtime for atomic in-place upgrades).
KVMAPP_DIR = "/kvmapp"

do_install() {
    install -d ${D}${KVMAPP_DIR}/server
    install -m 0755 ${B}/NanoKVM-Server ${D}${KVMAPP_DIR}/server/NanoKVM-Server

    # BMC CLI tools: rpiboot (Raspberry Pi boot control) and fw_env (U-Boot
    # environment r/w) -- both used for board/UEFI control.
    install -d ${D}${bindir}
    install -m 0755 ${B}/rpiboot ${D}${bindir}/rpiboot
    install -m 0755 ${B}/fw_env  ${D}${bindir}/fw_env

    # Service init script shipped by the app's packaging (start/stop/status via
    # start-stop-daemon), registered through oe-core sysvinit/update-rc.d.
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${GO_APP_DIR}/packaging/etc/init.d/S95nanokvm \
        ${D}${sysconfdir}/init.d/nanokvm
}

INITSCRIPT_NAME = "nanokvm"
INITSCRIPT_PARAMS = "defaults 95"

# Statically-linked Go binaries: Go does its own linking/stripping, so skip the
# LDFLAGS-injection and already-stripped QA checks that don't apply.
INSANE_SKIP:${PN} += "ldflags already-stripped"

FILES:${PN} = " \
    ${KVMAPP_DIR} \
    ${bindir}/rpiboot \
    ${bindir}/fw_env \
    ${sysconfdir}/init.d/nanokvm \
    "
