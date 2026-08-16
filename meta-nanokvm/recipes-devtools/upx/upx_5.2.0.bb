SUMMARY = "UPX, the Ultimate Packer for eXecutables"
DESCRIPTION = "Executable packer that rewrites an ELF into a self-extracting \
image. Built here as a native tool so nanokvm-server can compress its Go \
binary the way the upstream Makefile's `app` target does. riscv64 targets \
need UPX >= 5.0; earlier releases cannot pack the SG2002's binaries at all."
HOMEPAGE = "https://upx.github.io/"

# GPL-2.0-or-later plus the UPX exception (LICENSE), which grants the right to
# use and distribute a compressed program without it becoming a derived work.
# Only the host tool is built, so nothing here is shipped in the image.
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263 \
                    file://LICENSE;md5=353753597aa110e0ded3508408c6374a"

SRC_URI = "https://github.com/upx/upx/releases/download/v${PV}/upx-${PV}-src.tar.xz"
SRC_URI[sha256sum] = "af99e526d5759de94412aea1104d5e4ca406cb725295f8633ecc9e843dc1ce1c"

# The release "-src" tarball vendors every dependency (ucl, zlib, zstd,
# lzma-sdk, bzip2), unlike a bare git checkout which pulls them as submodules.
# So the build needs neither network access nor host libraries.
S = "${WORKDIR}/upx-${PV}-src"

inherit cmake

# cmake.bbclass leaves CMAKE_BUILD_TYPE unset and supplies its own flags; UPX
# reads the build type to decide on NDEBUG, and a packer running with asserts
# enabled is needlessly slow. OE's CFLAGS still win where they overlap.
EXTRA_OECMAKE = "-DCMAKE_BUILD_TYPE=Release"

BBCLASSEXTEND = "native"
