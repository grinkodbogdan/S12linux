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
# Let's Encrypt's newer roots, not yet in Mozilla's list: sites using them (Void's repository) are
# then verified directly, without the cross-signature that older TLS code (XBPS) fails on
EXTRA_ROOTS="
https://letsencrypt.org/certs/gen-y/root-yr.pem|d8a34bfb5df6b4a0592b0a9cd6b3b9a9335bd431cb2ac32243ab298cba24cbf2
https://letsencrypt.org/certs/gen-y/root-ye.pem|b8471c8049835b4097069ae3d43ea8235b510c0aa2699dedaaab9bc10be3d2c4
"
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
# Wi-Fi: wpa_supplicant and iw (with libnl), and b43-fwcutter for the Broadcom firmware
WIFI_SOURCES="
https://github.com/thom311/libnl/releases/download/libnl3_11_0/libnl-3.11.0.tar.gz|2a56e1edefa3e68a7c00879496736fdbf62fc94ed3232c0baba127ecfa76874d|libnl-3.11.0
https://w1.fi/releases/wpa_supplicant-2.12.tar.gz|08e23937e16d0155e55cab2b51f51fbe10d80a1aa91c4e15442645059b737ef6|wpa_supplicant-2.12
https://mirrors.edge.kernel.org/pub/software/network/iw/iw-6.17.tar.xz|7d182e498289ab39b257da6780d562e415377107f50358ee5b55b8cfe40b1e33|iw-6.17
https://bues.ch/b43/fwcutter/b43-fwcutter-019.tar.bz2|d6ea85310df6ae08e7f7e46d8b975e17fc867145ee249307413cfbe15d7121ce|b43-fwcutter-019
"
# Free firmware for Wi-Fi cards and USB dongles, from linux-firmware: path|sha256
FIRMWARE_URL=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain
FIRMWARE_TAG=20260810
FIRMWARE_FILES="
intel/iwlwifi/iwlwifi-5000-5.ucode|da259ae4b436f4d166aad2139f23694c2737e25288095140a1809d60fe676247
intel/iwlwifi/iwlwifi-5150-2.ucode|590d631088ad606a59318ba123dc744a02366d9dad84196ee5b078ca286ba749
intel/iwlwifi/iwlwifi-1000-5.ucode|8e5fae67b9b84d90fd806fb907707af0ede479351848547fbe8cdf1d74005f2b
rt2870.bin|251b8918391eac6415d60dca239e415aad0177e885376f2a17782e64fcbbe317
ath9k_htc/htc_9271-1.4.0.fw|78f7d592a95b419a02fde7440f30c606fc31a871cf0ce150ced40d4857173eb0
ath9k_htc/htc_7010-1.4.0.fw|737850aaf66ca13f82e0f5ca5e758f606f840c2f23c291350ab916bbf88e6f75
rtlwifi/rtl8192cufw_TMSC.bin|c083f2db36ef33d0ffffc979aba1d40f55fa9dc38dce50f26e0108a21c080286
rtlwifi/rtl8192cufw_A.bin|32846065b017c8b1d2d448e8c0ea2f5e741424809be49f2c6a6261339cd0b0cf
rtlwifi/rtl8192cufw_B.bin|479e5e6732b0e07cfcab4cc190b8c30465c0b7f8c0ed22d742bedd4412cbcf2a
rtlwifi/rtl8188eufw.bin|2ff74315287529dec2e50eb57d6e0c97d2116f28ae166773ccdf93b6360000c4
rtlwifi/rtl8188fufw.bin|e6189d6d7032b83f0ed4284629c43db66262449a866f33fd329e611239f62e1f
rtlwifi/rtl8723bu_nic.bin|a52b5a7be4841b4b2839eddf5122b3300a3610866abe4bb3c3c9e444c8ab7969
rtlwifi/rtl8192eu_nic.bin|b15bc955fba2fc3abc093affe62c9f7284a0b84d4f13f8ce55366488dc9aad8b
LICENSES/LICENCE.iwlwifi_firmware|16d5040c7cf851fc693b7542e20870935b99802533ea1bfd231c377a2305e5c5
LICENSES/LICENCE.ralink-firmware.txt|d7bec70668ddd4aae8fb4aa32870e54b49fcdb0b9b007aa9f54b53a1ac7461bd
LICENSES/LICENCE.rtlwifi_firmware.txt|a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e
LICENSES/LICENCE.open-ath9k-htc-firmware|83870e84c54aa76be973a78387b342c80ef0908b8cb82edde6d54c511b18c16c
"
# Wi-Fi regulatory database (which channels and power each country allows)
REGDB_URL=https://git.kernel.org/pub/scm/linux/kernel/git/wens/wireless-regdb.git/plain
REGDB_TAG=master-2026-05-30
REGDB_FILES="
regulatory.db|2fb33ca0074db573e05ef7dd50bb45b63c0ff98b7e852e1105ebad536fae8e6b
regulatory.db.p7s|c941c08f51c93e46722293b85631604c3740d86c3de0c75f79aef50d2e919179
LICENSE|678b0df753c86198fc496d1f1033429bbd57f101472132ee7eaaf9f5e0a7fae1
"
# Package manager: static XBPS from Void Linux (installs packages from Void's i686 repository)
XBPS_URL=https://repo-default.voidlinux.org/static/xbps-static-latest.i686-musl.tar.xz
XBPS_SHA256=929175fc8dd57b54292de3075cf0c7158940d7b1fc6664f0d5f98d93693136be
# Broadcom's driver, from which the firmware of the S12's BCM4312 Wi-Fi card is extracted at build
# time (b43-fwcutter). Broadcom does not allow the firmware to be redistributed: images built with
# it are for personal use. B43_FIRMWARE=0 builds an image without it (wifi-firmware then fetches it).
B43_URLS="https://sources.openwrt.org/broadcom-wl-5.100.138.tar.bz2 https://www.lwfinger.com/b43-firmware/broadcom-wl-5.100.138.tar.bz2"
B43_SHA256=f1e7067aac5b62b67b8b6e4c517990277804339ac16065eb13c731ff909ae46f
