SUMMARY = "NanoKVM — IP KVM server for LicheeRV Nano"
HOMEPAGE = "https://github.com/sipeed/NanoKVM"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://src/github.com/sipeed/NanoKVM/LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

inherit go-mod go update-rc.d

GO_IMPORT = "github.com/sipeed/NanoKVM"
SRCREV = "${AUTOREV}"
SRC_URI = " \
    git://github.com/sipeed/NanoKVM;branch=main;protocol=https \
    file://nanokvm.init \
    "

S = "${WORKDIR}/git"

# patchelf is used to point the binary at its bundled cvitek libs.
DEPENDS += "patchelf-native"

# The NanoKVM Go server lives in the server/ subdirectory (module
# 'NanoKVM-Server') and uses cgo against the pre-built cvitek libraries in
# server/dl_lib (-I../include -L../dl_lib -lkvm). Upstream ships no vendor/
# directory, so Go fetches the module dependencies over the network at build
# time. go.mod requires Go 1.24 while oe-core ships 1.22, so the toolchain
# auto-downloads the newer Go (GOTOOLCHAIN defaults to "auto").
do_compile[network] = "1"

GO_SERVER_DIR = "${B}/src/${GO_IMPORT}/server"
NANOKVM_LIBDIR = "${libdir}/nanokvm"

# go-mod.bbclass adds "${B}/.mod" to cleandirs (rm -rf before each compile), but
# Go marks the auto-downloaded toolchain (go1.24) directories read-only, so that
# rm fails with EPERM. Drop .mod from cleandirs: the module cache is persisted
# between runs (faster) and made writable at the end of do_compile so it can
# still be cleaned up later.
do_compile[cleandirs] = "${B}/bin ${B}/pkg"

do_compile() {
    cd ${GO_SERVER_DIR}

    export GOFLAGS="-mod=mod"
    export CGO_ENABLED="1"
    # go.mod requires Go 1.24 but oe-core ships 1.22; allow the toolchain to
    # download and switch to the required Go (needs network, enabled above).
    # go.bbclass sets GOENV=off, so GOSUMDB must be set explicitly for the
    # toolchain (and module) checksum verification to work.
    export GOTOOLCHAIN="auto"
    export GOSUMDB="sum.golang.org"
    # GOARCH/GOOS, the cross CC, CGO_* flags, GOPROXY and the module cache are
    # provided by go.bbclass / go-mod.bbclass.

    ${GO} build -v -trimpath -o ${B}/NanoKVM-Server .

    # Keep the module cache (incl. the read-only downloaded toolchain) writable
    # so bitbake/rm can clean ${B} later.
    chmod -R u+w ${B}/.mod 2>/dev/null || true
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/NanoKVM-Server ${D}${bindir}/nanokvm

    # Bundle the pre-built cvitek vision libraries the server links against and
    # point the binary's RPATH at them.
    install -d ${D}${NANOKVM_LIBDIR}
    install -m 0644 ${GO_SERVER_DIR}/dl_lib/*.so ${D}${NANOKVM_LIBDIR}/
    patchelf --set-rpath '${NANOKVM_LIBDIR}' ${D}${bindir}/nanokvm

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nanokvm.init ${D}${sysconfdir}/init.d/nanokvm

    install -d ${D}${datadir}/nanokvm
}

INITSCRIPT_NAME = "nanokvm"
INITSCRIPT_PARAMS = "defaults 99"

FILESEXTRAPATHS:prepend := "${THISDIR}/nanokvm:"

# The dl_lib blobs are pre-built, unversioned shared objects; relax the QA
# checks that do not apply to vendored binaries.
#
# file-rdeps: the cvitek vision libs (libkvm.so, libcvi_ispd2.so, ...) link
#   against json-c and OpenCV 4.9 (libjson-c.so.5, libopencv_*.so.409). For the
#   on-device vision/video feature to work at runtime those must be present in
#   the rootfs (add "json-c" + the opencv 4.9 lib packages to the image); the
#   dependency is skipped here because the providers are not pulled in by this
#   package alone.
INSANE_SKIP:${PN} += "already-stripped ldflags rpaths dev-so textrel file-rdeps"

FILES:${PN} = " \
    ${bindir}/nanokvm \
    ${NANOKVM_LIBDIR} \
    ${sysconfdir}/init.d/nanokvm \
    ${datadir}/nanokvm \
    "
