#!/bin/sh
# Builds S12 Linux: downloads the sources, then builds the kernel, BusyBox, the desktop, the web
# browser, the root file system and the bootable image.
#
# usage: ./build.sh [step...]
#   steps: fetch toolchain kernel busybox desk web rootfs image (default: all, in this order)
#
# Everything happens in $WORK (default: ./work). Needs a Linux x86_64 machine with gcc, make,
# flex, bison, bc, perl, xz, cpio, python3, curl, grub-pc-bin and xorriso.
set -e
TOP=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$TOP/work}
DL=$WORK/dl
JOBS=${JOBS:-$(nproc)}
. "$TOP/sources.sh"

log() { printf '\n== %s\n' "$*"; }

# Downloads a file into $DL and checks its SHA-256 (skipped for "-")
fetch_one() {
	url=$1 sum=$2 name=${3:-${1##*/}}
	if [ ! -s "$DL/$name" ]; then
		echo "downloading $name"
		curl -fL --retry 3 -o "$DL/$name.part" "$url"
		mv "$DL/$name.part" "$DL/$name"
	fi
	if [ "$sum" != - ]; then
		echo "$sum  $DL/$name" | sha256sum -c --quiet - || { echo "bad checksum: $name"; exit 1; }
	fi
}

step_fetch() {
	log "Downloading sources"
	mkdir -p "$DL"
	fetch_one "$LINUX_URL" "$LINUX_SHA256"
	fetch_one "$BUSYBOX_URL" "$BUSYBOX_SHA256"
	fetch_one "$ZIG_URL" "$ZIG_SHA256"
	fetch_one "$DEJAVU_URL" "$DEJAVU_SHA256"
	fetch_one "$CACERT_URL" - cacert.pem
	for w in $WEB_SOURCES; do
		fetch_one "$(echo $w | cut -d'|' -f1)" "$(echo $w | cut -d'|' -f2)"
	done
}

# Cross compiler: zig cc for i686 musl, static, tuned for the Atom (bonnell)
step_toolchain() {
	log "Setting up the cross compiler"
	if [ ! -x "$WORK/zig/zig" ]; then
		rm -rf "$WORK/zig" && mkdir -p "$WORK/zig"
		tar xJf "$DL/${ZIG_URL##*/}" -C "$WORK/zig" --strip-components=1
	fi
	mkdir -p "$WORK/bin"
	for t in cc c++; do
		cat > "$WORK/bin/i686-s12-$( [ $t = cc ] && echo gcc || echo g++ )" <<EOF
#!/bin/sh
# zig $t for i686 musl. Translates or drops the options zig does not support.
for a in "\$@"; do
	shift
	case "\$a" in
		-Wp,-MD,*) set -- "\$@" -MD -MF "\${a#-Wp,-MD,}" ;;
		-Wl,--trace|-Wl,--warn-common|-Wl,--fatal-warnings|-Wl,--verbose|-Wl,-Map,*|-Wl,--sort-common|-Wl,--sort-section,*) ;;
		*) set -- "\$@" "\$a" ;;
	esac
done
exec "$WORK/zig/zig" $t -target x86-linux-musl -mcpu=bonnell "\$@"
EOF
	done
	for t in ar ranlib objcopy; do
		printf '#!/bin/sh\nexec "%s" %s "$@"\n' "$WORK/zig/zig" $t > "$WORK/bin/i686-s12-$t"
	done
	# zig has no strip: the host's binutils handle 32-bit x86 ELF files
	printf '#!/bin/sh\nexec strip "$@"\n' > "$WORK/bin/i686-s12-strip"
	printf '#!/bin/sh\nexec nm "$@"\n' > "$WORK/bin/i686-s12-nm"
	chmod +x "$WORK"/bin/*
}

export PATH="$WORK/bin:$PATH"
CROSS=i686-s12

step_kernel() {
	log "Building Linux $LINUX_VERSION"
	SRC=$WORK/linux-$LINUX_VERSION
	[ -d "$SRC" ] || tar xJf "$DL/${LINUX_URL##*/}" -C "$WORK"
	mkdir -p "$WORK/kbuild"
	make -C "$SRC" ARCH=i386 O="$WORK/kbuild" i386_defconfig > /dev/null
	cat "$TOP/kernel/s12.config" >> "$WORK/kbuild/.config"
	make -C "$SRC" ARCH=i386 O="$WORK/kbuild" olddefconfig > /dev/null 2>&1
	make -C "$WORK/kbuild" ARCH=i386 -j"$JOBS" bzImage
	cp "$WORK/kbuild/arch/x86/boot/bzImage" "$WORK/bzImage"
}

step_busybox() {
	log "Building BusyBox $BUSYBOX_VERSION"
	SRC=$WORK/busybox-$BUSYBOX_VERSION
	[ -d "$SRC" ] || tar xjf "$DL/${BUSYBOX_URL##*/}" -C "$WORK"
	cp "$TOP/busybox/config" "$SRC/.config"
	yes "" | make -C "$SRC" oldconfig > /dev/null
	make -C "$SRC" CC=$CROSS-gcc AR=$CROSS-ar HOSTCC=gcc STRIP=$CROSS-strip -j"$JOBS" busybox busybox.links
}

step_desk() {
	log "Building the desktop, init and the sound test"
	rm -rf "$WORK/desk" && cp -r "$TOP/desk" "$WORK/desk"
	make -C "$WORK/desk" CC=$CROSS-gcc STRIP=$CROSS-strip
}

# NetSurf, statically linked, with its libraries (zlib, libpng, libjpeg, freetype, OpenSSL, curl,
# expat). The curl command line tool comes from the same build.
step_web() {
	log "Building the web browser"
	W=$WORK/web
	SYS=$W/sysroot
	mkdir -p "$W/src" "$W/logs" "$SYS"
	for w in $WEB_SOURCES; do
		f=$(echo $w | cut -d'|' -f1); f=${f##*/}
		d=$(echo $w | cut -d'|' -f3)
		[ -d "$W/src/$d" ] || tar xf "$DL/$f" -C "$W/src"
	done
	(
		export CC=$CROSS-gcc AR=$CROSS-ar RANLIB=$CROSS-ranlib STRIP=$CROSS-strip
		export CFLAGS="-O2 -fno-stack-protector" CPPFLAGS="-I$SYS/include" LDFLAGS="-L$SYS/lib -static"
		export PKG_CONFIG_PATH=$SYS/lib/pkgconfig PKG_CONFIG_LIBDIR=$SYS/lib/pkgconfig
		HOSTARG="--host=i686-linux-musl --prefix=$SYS --disable-shared --enable-static"
		L=$W/logs
		if [ ! -f $SYS/lib/libz.a ]; then echo zlib
			cd $W/src/zlib-1.3.1 && CHOST=i686-linux-musl ./configure --static --prefix=$SYS > $L/zlib.log 2>&1 && make -j$JOBS >> $L/zlib.log 2>&1 && make install >> $L/zlib.log 2>&1
		fi
		if [ ! -f $SYS/lib/libpng.a ]; then echo libpng
			cd $W/src/libpng-1.6.44 && ./configure $HOSTARG > $L/png.log 2>&1 && make -j$JOBS >> $L/png.log 2>&1 && make install >> $L/png.log 2>&1
		fi
		if [ ! -f $SYS/lib/libjpeg.a ]; then echo libjpeg
			cd $W/src/jpeg-9f && ./configure $HOSTARG > $L/jpeg.log 2>&1 && make -j$JOBS >> $L/jpeg.log 2>&1 && make install >> $L/jpeg.log 2>&1
		fi
		if [ ! -f $SYS/lib/libfreetype.a ]; then echo freetype
			cd $W/src/freetype-2.13.3 && ./configure $HOSTARG --with-zlib=yes --with-png=yes --with-bzip2=no --with-harfbuzz=no --with-brotli=no > $L/freetype.log 2>&1 && make -j$JOBS >> $L/freetype.log 2>&1 && make install >> $L/freetype.log 2>&1
		fi
		if [ ! -f $SYS/lib/libssl.a ]; then echo openssl
			cd $W/src/openssl-3.0.15 && ./Configure linux-x86 no-asm no-shared no-tests no-dso no-engine no-async no-afalgeng no-comp no-ssl3 no-weak-ssl-ciphers --prefix=$SYS --libdir=lib --openssldir=/etc/ssl CC=$CC AR=$AR RANLIB=$RANLIB > $L/openssl.log 2>&1 && make -j$JOBS build_libs >> $L/openssl.log 2>&1 && make install_dev >> $L/openssl.log 2>&1
		fi
		if [ ! -f $SYS/lib/libcurl.a ]; then echo curl
			cd $W/src/curl-8.10.1 && ./configure $HOSTARG --with-openssl=$SYS --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt --with-zlib=$SYS \
				--without-libpsl --without-zstd --without-brotli --without-nghttp2 --without-libidn2 --without-librtmp --without-libssh2 \
				--disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap \
				--disable-smb --disable-smtp --disable-gopher --disable-mqtt --disable-manual --disable-docs --disable-ipv6 --disable-threaded-resolver \
				--enable-cookies --disable-ntlm > $L/curl.log 2>&1 && make -j$JOBS >> $L/curl.log 2>&1 && make -C lib install >> $L/curl.log 2>&1 && make -C include install >> $L/curl.log 2>&1 && make install-pkgconfigDATA install-binSCRIPTS >> $L/curl.log 2>&1
		fi
		if [ ! -f $SYS/lib/libexpat.a ]; then echo expat
			cd $W/src/expat-2.6.3 && ./configure $HOSTARG --without-docbook --without-examples --without-tests > $L/expat.log 2>&1 && make -j$JOBS >> $L/expat.log 2>&1 && make install >> $L/expat.log 2>&1
		fi
		echo netsurf
		NS=$W/src/netsurf-all-3.11
		if [ ! -f $NS/.s12-patched ]; then
			patch -d $NS -p1 < "$TOP/netsurf/netsurf-s12.patch"
			touch $NS/.s12-patched
		fi
		export PKG_CONFIG_PATH=
		export CFLAGS="-I$SYS/include -O2 -fno-stack-protector" LDFLAGS="-L$SYS/lib"
		unset CPPFLAGS
		make -C $NS TARGET=framebuffer HOST=$CROSS BUILD=x86_64-linux-gnu BUILD_CC=gcc NETSURF_CONFIG="HOST=$CROSS CC=$CC" > $L/netsurf.log 2>&1
		rm -rf $W/stage
		PKG_CONFIG_PATH=$NS/inst-framebuffer/lib/pkgconfig make -C $NS/netsurf install TARGET=framebuffer HOST=$CROSS CC=$CC \
			PREFIX=/usr DESTDIR=$W/stage > $L/netsurf-install.log 2>&1
	)
}

step_rootfs() {
	log "Staging the root file system"
	R=$WORK/root
	rm -rf "$R"
	mkdir -p $R/bin $R/sbin $R/usr/bin $R/usr/sbin $R/etc $R/dev $R/proc $R/sys $R/tmp $R/run $R/mnt $R/root
	BB=$WORK/busybox-$BUSYBOX_VERSION
	$CROSS-strip -o $R/bin/busybox $BB/busybox_unstripped
	while read -r link; do
		case $link in
			/bin/*) ln -s busybox $R$link ;;
			/usr/bin/* | /usr/sbin/*) ln -s ../../bin/busybox $R$link ;;
			/sbin/*) ln -s ../bin/busybox $R$link ;;
		esac
	done < $BB/busybox.links
	cp $WORK/desk/minit $R/sbin/init
	cp $WORK/desk/minit $R/sbin/textinit
	for c in poweroff reboot halt; do ln -sf init $R/sbin/$c; done
	cp $WORK/desk/desk $R/bin/desk
	cp $WORK/desk/soundtest $R/usr/bin/soundtest
	cp -a "$TOP/rootfs/." $R/
	ln -s /run/resolv.conf $R/etc/resolv.conf
	ln -s /proc/mounts $R/etc/mtab
	# Web browser: NetSurf with its resources, fonts, and the Mozilla CA certificates
	mkdir -p $R/usr/share/netsurf $R/usr/share/fonts $R/etc/ssl/certs $R/root/.netsurf
	$CROSS-strip -o $R/usr/bin/netsurf-fb $WORK/web/stage/usr/bin/netsurf-fb
	cp $WORK/web/stage/usr/share/netsurf/* $R/usr/share/netsurf/
	[ -d $WORK/dejavu ] || { mkdir -p $WORK/dejavu && tar xjf "$DL/${DEJAVU_URL##*/}" -C $WORK/dejavu --strip-components=1; }
	for f in DejaVuSans DejaVuSans-Bold DejaVuSans-Oblique DejaVuSans-BoldOblique DejaVuSerif \
		DejaVuSerif-Bold DejaVuSerif-Italic DejaVuSansMono DejaVuSansMono-Bold; do
		cp $WORK/dejavu/ttf/$f.ttf $R/usr/share/fonts/
	done
	cp "$DL/cacert.pem" $R/etc/ssl/certs/ca-certificates.crt
	$CROSS-strip -o $R/usr/bin/curl $WORK/web/src/curl-8.10.1/src/curl
	cp "$TOP/netsurf/Choices" $R/root/.netsurf/Choices
	cp "$TOP/netsurf/Choices" $R/usr/share/netsurf/Choices
	du -sh $R
}

step_image() {
	log "Building the boot image"
	I=$WORK/iso
	rm -rf $I && mkdir -p $I/boot/grub
	cp $WORK/bzImage $I/boot/linux
	(cd $WORK/root && find . | sort | cpio -o -H newc --owner=0:0 --quiet) | xz -9 --check=crc32 > $I/boot/initramfs.xz
	cp "$TOP/boot/grub.cfg" $I/boot/grub/grub.cfg
	grub-mkrescue -o $WORK/s12-linux.iso $I 2> /dev/null
	ls -l $WORK/s12-linux.iso
	sha256sum $WORK/s12-linux.iso
}

STEPS=${*:-fetch toolchain kernel busybox desk web rootfs image}
for s in $STEPS; do
	step_$s
done
