#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/env.sh"
# Minimum SDK family tested by this port; CI updates the official packages.
compiler=$(powerpc-eabi-gcc -dumpfullversion)
if [ "${compiler%%.*}" -lt 16 ]; then
	echo "Use devkitPPC r50 or newer (GCC 16.1+); found $compiler" >&2
	exit 1
fi
header="$DEVKITPRO/libogc/include/ogc/libversion.h"
major=$(awk '/^#define _V_MAJOR_/ {print $3}' "$header")
minor=$(awk '/^#define _V_MINOR_/ {print $3}' "$header")
if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 1 ]; }; then
	echo "Use libogc 3.1.0 or newer; found $major.$minor" >&2
	exit 1
fi
powerpc-eabi-gcc --version | head -n 1
awk '/^#define _V_(MAJOR|MINOR|PATCH)_/ {printf "%s ", $3} END {print "(libogc)"}' "$header"
