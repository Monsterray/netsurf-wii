#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LIBNSFB="$SCRIPT_DIR/.deps/netsurf-workspace/libnsfb"
TEMP=$(mktemp -d)
trap 'rm -rf "$TEMP"' EXIT
mkdir -p "$TEMP/machine"
# GEKKO's branches read bytes explicitly; the host needs no SDK endian header.
cat > "$TEMP/machine/endian.h" <<'HEADER'
#ifdef __APPLE__
#include_next <machine/endian.h>
#else
#include <endian.h>
#endif
HEADER
cc -std=c99 -DGEKKO -I"$TEMP" -I"$LIBNSFB/include" -I"$LIBNSFB/src" \
	-Dmalloc=tracked_malloc -c "$LIBNSFB/src/plot/api.c" -o "$TEMP/api.o"
cc -std=c99 -DGEKKO -I"$TEMP" -I"$LIBNSFB/include" -I"$LIBNSFB/src" \
	"$SCRIPT_DIR/test-render.c" "$TEMP/api.o" \
	"$LIBNSFB/src/plot/32bpp-xrgb8888.c" "$LIBNSFB/src/plot/util.c" \
	"$LIBNSFB/src/palette.c" -lm -o "$TEMP/render"
"$TEMP/render"
echo 'PASS: raw RGBA blits, scaling, tiling, padded rows, zero scratch allocation, cursor colors'

# Built-in icons/cursors must use canonical integers on both target endians.
HOST_PKG_CONFIG=${HOST_PKG_CONFIG:-$(command -v pkg-config)}
PNG_CFLAGS=$(unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR; "$HOST_PKG_CONFIG" --cflags libpng)
PNG_LIBS=$(unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR; "$HOST_PKG_CONFIG" --libs libpng)
cc $PNG_CFLAGS "$SCRIPT_DIR/../tools/convert_image.c" $PNG_LIBS -o "$TEMP/convert"
"$TEMP/convert" "$SCRIPT_DIR/wii-test.png" "$TEMP/fixture.c" fixture
python3 - "$TEMP/fixture.c" <<'PYTHON'
import pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text()
assert 'static uint32_t fixture_pixdata[]' in text
values = re.findall(r'0x([0-9a-f]{8})', text)
assert 'ff0000ff' in values  # opaque red
assert 'ff00ff00' in values  # opaque green
assert 'ffff0000' in values  # opaque blue
print('PASS: generated icons and cursors use endian-independent ABGR colors')
PYTHON
