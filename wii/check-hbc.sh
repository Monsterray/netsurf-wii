#!/bin/bash
# Keep the SDK and host tools on the reviewed HBC-Reborn revision.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/hbc-reborn.env"
SOURCE=${1:?Pass the hbc-reborn checkout}
actual=$(git -C "$SOURCE" rev-parse HEAD)
if [ "$actual" != "$HBC_REBORN_COMMIT" ]; then
    echo "HBC-Reborn $HBC_REBORN_VERSION requires $HBC_REBORN_COMMIT; found $actual in $SOURCE" >&2
    echo 'Update that checkout, or review a newer revision and update wii/hbc-reborn.env.' >&2
    exit 1
fi
# Ignore unrelated local artifacts; do not silently build modified SDK/tools.
git -C "$SOURCE" diff --quiet HEAD -- sdk tools channel/channelapp || {
    echo "Uncommitted HBC SDK, host tools or channel source changes in $SOURCE" >&2
    exit 1
}
printf 'HBC-Reborn %s (%s)\n' "$HBC_REBORN_VERSION" "$actual"
