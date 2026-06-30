SUMMARY = "MaixCDK runtime skeleton for NanoKVM (Sophgo SG2002)"
DESCRIPTION = "Reproduces the upstream Buildroot maix-cdk package as configured by \
build-nanokvm.sh. In that configuration (shrink=y: MAIX_CDK_ALL_PROJECTS, \
MAIX_CDK_ALL_EXAMPLES and MAIX_CDK_ALL_DEPENDENCIES all unset) the package's \
MAIX_CDK_BUILD_CMDS removes every example/project/heavy 3rd_party component and \
runs no `maixcdk build` at all; its only target footprint is the /maixapp runtime \
directory skeleton + a default sys_conf.ini. This recipe provides exactly that, \
rather than building the full MaixCDK framework (which the NanoKVM does not use)."

# No MaixCDK code is shipped (the upstream package builds none in this config); the
# package contains only the runtime directory skeleton + a config file authored here.
LICENSE = "CLOSED"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# Nothing to fetch/configure/compile -- this is the runtime skeleton only.
do_fetch[noexec] = "1"
do_unpack[noexec] = "1"
do_patch[noexec] = "1"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    # /maixapp runtime layout (matches upstream MAIX_CDK_INSTALL_TARGET_CMDS).
    install -d ${D}/maixapp/lib
    install -d ${D}/maixapp/apps
    install -d ${D}/maixapp/share/font
    install -d ${D}/maixapp/share/icon
    install -d ${D}/maixapp/share/picture
    install -d ${D}/maixapp/share/video
    install -d ${D}/maixapp/tmp

    cat > ${D}/maixapp/sys_conf.ini <<'EOF'
[language]
locale=en
[comm]
method=uart
EOF
}

FILES:${PN} = "/maixapp"
