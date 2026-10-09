#!/bin/bash
# Use HBC's external SDK without writing to its checkout.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/env.sh"
SOURCE=${1:?Pass the hbc-reborn checkout}
"$SCRIPT_DIR/check-hbc.sh" "$SOURCE"
[ -f "$SOURCE/sdk/hbc_agent/Makefile" ] || { echo "Missing HBC SDK: $SOURCE" >&2; exit 1; }
mkdir -p "$SCRIPT_DIR/.deps/hbc-sdk/build"
ln -sfn "$(cd "$SOURCE" && pwd)" "$SCRIPT_DIR/.deps/hbc-sdk/source"
cp "$SOURCE/sdk/hbc_agent.h" "$SOURCE/sdk/hbc_netlog.h" "$SCRIPT_DIR/.deps/hbc-sdk/"
make -B -C "$SCRIPT_DIR/.deps/hbc-sdk/source/sdk/hbc_agent" -j4 \
    "BUILD=$SCRIPT_DIR/.deps/hbc-sdk/build" \
    "OUT=$SCRIPT_DIR/.deps/hbc-sdk/build/libhbcagent.a"
