#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SUPPORT_PREFIX="$SCRIPT_DIR/.deps/netsurf-workspace/inst-powerpc-eabi"
NETWORK_PREFIX="$SCRIPT_DIR/.deps/network"
ICONV_PREFIX="$SCRIPT_DIR/.deps/iconv"
OPTIONAL_PREFIX="$SCRIPT_DIR/.deps/optional/prefix"
INPUT_PREFIX="$SCRIPT_DIR/.deps/input/prefix"
HOST_PKG_CONFIG=${HOST_PKG_CONFIG:-$(command -v pkg-config)}
HOST_BUILD=$(cc -dumpmachine 2>/dev/null || printf '%s' arm64-apple-darwin)
HOST_TOOL_PREFIX="$SCRIPT_DIR/.deps/netsurf-workspace/inst-$HOST_BUILD"
PACKAGE_DIR="$SCRIPT_DIR/dist/apps/netsurf"
RODIN_FONT_DIR=${RODIN_FONT_DIR:-"$HOME/Library/Fonts"}
RODIN_REGULAR_SOURCE=${RODIN_REGULAR_SOURCE:-"$RODIN_FONT_DIR/FOT-RodinNTLGPro-M.otf"}
RODIN_BOLD_SOURCE=${RODIN_BOLD_SOURCE:-"$RODIN_FONT_DIR/FOT-RodinNTLGPro-B.otf"}

. "$SCRIPT_DIR/env.sh"
"$SCRIPT_DIR/check-toolchain.sh"
for tool_dir in /usr/local/opt/bison/bin /usr/local/opt/flex/bin \
	/opt/homebrew/opt/bison/bin /opt/homebrew/opt/flex/bin; do
	if [ -d "$tool_dir" ]; then
		PATH="$tool_dir:$PATH"
	fi
done
export PATH
PATH="$HOST_TOOL_PREFIX/bin:$PATH"
export PATH

for required in \
	"$SUPPORT_PREFIX/lib/pkgconfig/libnsfb.pc" \
	"$NETWORK_PREFIX/lib/libcurl.a" \
	"$ICONV_PREFIX/lib/libiconv.a" \
	"$OPTIONAL_PREFIX/lib/libhpdf.a" \
	"$INPUT_PREFIX/lib/libwupc.a" \
	"$INPUT_PREFIX/include/wupc/wupc.h" \
	"$HOST_TOOL_PREFIX/bin/nsgenbind" \
	"$RODIN_REGULAR_SOURCE" \
	"$RODIN_BOLD_SOURCE" \
	"$SCRIPT_DIR/cacert.pem"; do
	if [ ! -f "$required" ]; then
		echo "Missing browser dependency: $required" >&2
		echo "See wii/README.md for the bootstrap procedure." >&2
		exit 1
	fi
done

# Do not silently link the old event dispatcher from an existing dependency tree.
if ! git -C "$SCRIPT_DIR/.deps/netsurf-workspace/libdom" apply --reverse --check \
        "$SCRIPT_DIR/patches/libdom-event-dispatch.patch" 2>/dev/null ||
        [ "$SCRIPT_DIR/.deps/netsurf-workspace/libdom/src/core/node.c" -nt "$SUPPORT_PREFIX/lib/libdom.a" ]; then
    echo 'Rebuild browser dependencies for the libdom event-dispatch fix.' >&2
    exit 1
fi

cp "$SCRIPT_DIR/Makefile.config.wii" "$SOURCE_ROOT/Makefile.config"

export PKG_CONFIG_LIBDIR="$OPTIONAL_PREFIX/lib/pkgconfig:$SUPPORT_PREFIX/lib/pkgconfig:$NETWORK_PREFIX/lib/pkgconfig:$DEVKITPRO/portlibs/wii/lib/pkgconfig:$DEVKITPRO/portlibs/ppc/lib/pkgconfig"
export CFLAGS="-DBUILDING_LIBICONV=0 -DGEKKO -I$INPUT_PREFIX/include -I$OPTIONAL_PREFIX/include -I$ICONV_PREFIX/include -I$NETWORK_PREFIX/include -I$SUPPORT_PREFIX/include -I$DEVKITPRO/libogc/include -I$DEVKITPRO/portlibs/wii/include -I$DEVKITPRO/portlibs/ppc/include -mrvl -mcpu=750 -meabi -mhard-float"
export LDFLAGS="-L$INPUT_PREFIX/lib -L$OPTIONAL_PREFIX/lib -L$ICONV_PREFIX/lib -L$NETWORK_PREFIX/lib -L$SUPPORT_PREFIX/lib -L$DEVKITPRO/libogc/lib/wii -L$DEVKITPRO/portlibs/wii/lib -L$DEVKITPRO/portlibs/ppc/lib -mrvl -mcpu=750 -meabi -mhard-float"
# Optional development agent; build external SDK objects inside ignored deps.
HBC_AGENT=${HBC_AGENT:-0}
HBC_AGENT_CRASH_TEST=${HBC_AGENT_CRASH_TEST:-0}
case "$HBC_AGENT_CRASH_TEST:$HBC_AGENT" in 0:0|0:1|1:1) ;; *) echo "Crash probe requires HBC_AGENT=1 and a 0/1 flag" >&2; exit 1;; esac
case "$HBC_AGENT" in 0|1) ;; *) echo 'HBC_AGENT must be 0 or 1' >&2; exit 1;; esac
if [ "$HBC_AGENT" = 1 ]; then
    HBC_AGENT_ROOT=${HBC_AGENT_ROOT:-"$SOURCE_ROOT/../hbc-reborn"}
    "$SCRIPT_DIR/build-agent.sh" "$HBC_AGENT_ROOT"
    export CFLAGS="$CFLAGS -DNETSURF_HBC_AGENT -I$SCRIPT_DIR/.deps/hbc-sdk"
    export LDFLAGS="$LDFLAGS -L$SCRIPT_DIR/.deps/hbc-sdk/build"
    if [ "$HBC_AGENT_CRASH_TEST" = 1 ]; then
        export CFLAGS="$CFLAGS -DNETSURF_HBC_AGENT_CRASH_TEST"
    fi
fi
# Only the agent and its two callers include these instrumentation flags.
if [ "$(cat "$SCRIPT_DIR/.deps/browser-agent-mode" 2>/dev/null || true)" != "$HBC_AGENT:$HBC_AGENT_CRASH_TEST" ]; then
    for source in wii_agent wii_compat gui; do
        rm -f "$SOURCE_ROOT/build/powerpc-eabi-framebuffer/frontends_framebuffer_$source.o"
    done
fi
printf '%s\n' "$HBC_AGENT:$HBC_AGENT_CRASH_TEST" > "$SCRIPT_DIR/.deps/browser-agent-mode"
export NETSURF_BUILD_USER=quatric
export NETSURF_BUILD_NAME=quatric
export NETSURF_BUILD_ROOT=netsurf-wii
export HOSTNAME=wii-build

BUILD_LIBPNG_CFLAGS=$(unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR; "$HOST_PKG_CONFIG" --cflags libpng)
BUILD_LIBPNG_LDFLAGS=$(unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR; "$HOST_PKG_CONFIG" --libs libpng)

make -C "$SOURCE_ROOT" \
	TARGET=framebuffer HOST=powerpc-eabi \
	CC=powerpc-eabi-gcc CXX=powerpc-eabi-g++ \
	PKG_CONFIG="$SCRIPT_DIR/powerpc-eabi-ns-pkg-config" \
	BUILD_LIBPNG_CFLAGS="$BUILD_LIBPNG_CFLAGS" \
	BUILD_LIBPNG_LDFLAGS="$BUILD_LIBPNG_LDFLAGS" HBC_AGENT="$HBC_AGENT" \
	"$@"

mkdir -p "$PACKAGE_DIR"
elf2dol "$SOURCE_ROOT/nsfb" "$PACKAGE_DIR/boot.dol"
if ! grep -Fq '<coder>quatric</coder>' "$SCRIPT_DIR/meta.xml"; then
	echo "Refusing to package meta.xml without the quatric coder credit" >&2
	exit 1
fi

cp "$SCRIPT_DIR/meta.xml" "$SCRIPT_DIR/icon.png" "$SCRIPT_DIR/cacert.pem" \
	"$SCRIPT_DIR/js-smoke.html" "$SCRIPT_DIR/wii-test.html" \
	"$SCRIPT_DIR/wii-test.png" "$SCRIPT_DIR/wii-test.bin" "$PACKAGE_DIR/"
cp "$SCRIPT_DIR/adblock-hosts.txt" "$SCRIPT_DIR/adblock-allow.txt" "$PACKAGE_DIR/"

for resource in adblock.css credits.html default.css internal.css \
	licence.html netsurf.png quirks.css welcome.html; do
	cp -L "$SOURCE_ROOT/frontends/framebuffer/res/$resource" "$PACKAGE_DIR/"
done
cp "$SOURCE_ROOT/frontends/framebuffer/res/en/Messages" \
	"$PACKAGE_DIR/Messages"
mkdir -p "$PACKAGE_DIR/fonts"
cp "$RODIN_REGULAR_SOURCE" "$PACKAGE_DIR/fonts/RodinNTLG-M.otf"
cp "$RODIN_BOLD_SOURCE" "$PACKAGE_DIR/fonts/RodinNTLG-B.otf"
chmod 0644 "$PACKAGE_DIR/fonts/RodinNTLG-M.otf" \
	"$PACKAGE_DIR/fonts/RodinNTLG-B.otf"

{
	printf "HBC_AGENT=%s\nHBC_AGENT_CRASH_TEST=%s\n" "$HBC_AGENT" "$HBC_AGENT_CRASH_TEST"
	powerpc-eabi-gcc --version | head -n 1
	awk '/^#define _V_(MAJOR|MINOR|PATCH)_/ {print}' "$DEVKITPRO/libogc/include/ogc/libversion.h"
	git -C "$SOURCE_ROOT" rev-parse HEAD
	if [ "$HBC_AGENT" = 1 ]; then
		printf "HBC_SDK_COMMIT="
		git -C "$HBC_AGENT_ROOT" rev-parse HEAD
	fi
	for dependency in "$SCRIPT_DIR/.deps/netsurf-workspace/"*/.git; do
		git -C "${dependency%/.git}" log -1 --format='%h %s'
	done
} > "$PACKAGE_DIR/build-info.txt"
echo "Packaged Wii browser at $PACKAGE_DIR"
