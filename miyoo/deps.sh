#!/bin/sh
# Builds the libraries the Miyoo Mini Plus build links statically, for ARMv7 / glibc 2.28,
# into build-miyoo/prefix. Run inside the es-zig container from the repository root:
#   docker run --rm -v "$PWD:/es" -w /es es-zig sh miyoo/deps.sh
# Each archive's sha256 is recorded in miyoo/deps.lock on first download and checked after.
set -eu
ROOT=$(pwd)
CROSS=$ROOT/miyoo/cross
PREFIX=$ROOT/build-miyoo/prefix
WORK=$ROOT/build-miyoo/deps
LOCK=$ROOT/miyoo/deps.lock
JOBS=${JOBS:-2}
export CC=$CROSS/zcc CXX=$CROSS/zcxx AR=$CROSS/zar RANLIB=$CROSS/zranlib
mkdir -p "$PREFIX/lib/pkgconfig" "$PREFIX/include" "$WORK"
touch "$LOCK"

# fetch <name> <url>: download into $WORK/<name>.tar.*, check or record the sha256, unpack to $WORK/<name>.
fetch() {
	name=$1 url=$2 file=$WORK/$1.archive
	[ -f "$file" ] || curl -fsSL "$url" -o "$file"
	sum=$(sha256sum "$file" | cut -d' ' -f1)
	known=$(grep "^$name " "$LOCK" | cut -d' ' -f2 || true)
	if [ -z "$known" ]; then
		echo "$name $sum $url" >> "$LOCK"
	elif [ "$known" != "$sum" ]; then
		echo "sha256 mismatch for $name: expected $known, got $sum" >&2
		exit 1
	fi
	rm -rf "$WORK/$name"
	mkdir -p "$WORK/$name"
	tar -xf "$file" -C "$WORK/$name" --strip-components=1
}

cmake_build() { # dir, extra args...
	dir=$1; shift
	cmake -S "$dir" -B "$dir/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$CROSS/toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=OFF \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON "$@" > "$dir/configure.log" 2>&1 || { tail -30 "$dir/configure.log"; exit 1; }
	ninja -C "$dir/build" -j "$JOBS" install > "$dir/build.log" 2>&1 || { tail -30 "$dir/build.log"; exit 1; }
}

step() { echo "== $1"; }

step zlib
fetch zlib https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
cmake_build "$WORK/zlib" -DZLIB_BUILD_EXAMPLES=OFF
rm -f "$PREFIX"/lib/libz.so*

step minizip
# minizip ships inside zlib (contrib/minizip); only the zip/unzip readers are needed.
M=$WORK/zlib/contrib/minizip
mkdir -p "$PREFIX/include/minizip"
for f in ioapi unzip zip; do $CC -O2 -c "$M/$f.c" -I"$PREFIX/include" -o "$WORK/minizip-$f.o"; done
$AR rcs "$PREFIX/lib/libminizip.a" "$WORK"/minizip-*.o
cp "$M/ioapi.h" "$M/unzip.h" "$M/zip.h" "$PREFIX/include/minizip/"
cat > "$PREFIX/lib/pkgconfig/minizip.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: minizip
Description: zip and unzip from zlib contrib
Version: 1.3.1
Requires.private: zlib
Libs: -L\${libdir} -lminizip -lz
Cflags: -I\${includedir}/minizip -I\${includedir}
EOF

step libpng
fetch libpng https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.44.tar.gz
cmake_build "$WORK/libpng" -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_ARM_NEON=on \
	-DZLIB_ROOT="$PREFIX"

step libjpeg-turbo
fetch libjpeg-turbo https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.0.4/libjpeg-turbo-3.0.4.tar.gz
cmake_build "$WORK/libjpeg-turbo" -DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF -DWITH_SIMD=OFF

step libmad
# The maintained fork with a CMake build (upstream 0.15.1b needs autotools from 2004).
fetch libmad https://codeberg.org/tenacityteam/libmad/archive/0.16.4.tar.gz
cmake_build "$WORK/libmad" -DEXAMPLE=OFF

step openal-soft
# zig's libc headers have <linux/soundcard.h> but not glibc's <sys/soundcard.h> wrapper.
mkdir -p "$PREFIX/include/sys"
echo '#include <linux/soundcard.h>' > "$PREFIX/include/sys/soundcard.h"
fetch openal-soft https://github.com/kcat/openal-soft/archive/refs/tags/1.24.1.tar.gz
cmake_build "$WORK/openal-soft" -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF \
	-DALSOFT_TESTS=OFF -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF \
	-DALSOFT_BACKEND_OSS=ON -DALSOFT_REQUIRE_OSS=ON -DALSOFT_BACKEND_ALSA=OFF \
	-DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_JACK=OFF \
	-DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_PORTAUDIO=OFF -DALSOFT_BACKEND_SDL2=OFF \
	-DALSOFT_BACKEND_WAVE=OFF -DALSOFT_EMBED_HRTF_DATA=OFF -DOSS_INCLUDE_DIR="$PREFIX/include"

step SDL2
# Events, timers and threads only: the screen and buttons are driven directly.
fetch sdl2 https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
cmake_build "$WORK/sdl2" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_AUDIO=OFF \
	-DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF \
	-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
	-DSDL_VULKAN=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=OFF \
	-DSDL_DUMMYVIDEO=ON -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_FCITX=OFF

step done
ls -la "$PREFIX/lib"/*.a
