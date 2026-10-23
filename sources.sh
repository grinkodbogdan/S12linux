# Sources of S12 Linux, with their SHA-256 checksums.
LINUX_VERSION=7.3
LINUX_URL=https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.tar.xz
LINUX_SHA256=87363c24f10e06321eb362765521094e94471f6bff8bce2d6b3e40f39f004953
BUSYBOX_VERSION=1.37.0
BUSYBOX_URL=https://busybox.net/downloads/busybox-1.37.0.tar.bz2
BUSYBOX_SHA256=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
# Cross compiler: zig cc targets i686 musl without a separate toolchain build
ZIG_URL=https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz
ZIG_SHA256=d45312e61ebcc48032b77bc4cf7fd6915c11fa16e4aad116b66c9468211230ea
DEJAVU_URL=https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
DEJAVU_SHA256=fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
# Mozilla's CA certificates, as packaged by curl (updated regularly, so not checksummed)
CACERT_URL=https://curl.se/ca/cacert.pem
# Web browser and its libraries: url|sha256|directory
WEB_SOURCES="
https://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-3.11.tar.gz|4dea880ff3c2f698bfd62c982b259340f9abcd7f67e6c8eb2b32c61f71644b7b|netsurf-all-3.11
https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz|9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23|zlib-1.3.1
https://download.sourceforge.net/libpng/libpng-1.6.44.tar.xz|60c4da1d5b7f0aa8d158da48e8f8afa9773c1c8baa5d21974df61f1886b8ce8e|libpng-1.6.44
https://ijg.org/files/jpegsrc.v9f.tar.gz|04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b|jpeg-9f
https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz|0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289|freetype-2.13.3
https://www.openssl.org/source/openssl-3.0.15.tar.gz|23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533|openssl-3.0.15
https://curl.se/download/curl-8.10.1.tar.xz|73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee|curl-8.10.1
https://github.com/libexpat/libexpat/releases/download/R_2_6_3/expat-2.6.3.tar.xz|274db254a6979bde5aad404763a704956940e465843f2a9bd9ed7af22e2c0efc|expat-2.6.3
"
