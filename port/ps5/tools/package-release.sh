#!/usr/bin/env bash
# SpeedBreaker PS5: a release's downloads from the app folder link.sh staged:
#   build/ps5/PPSA90505.zip   the app folder at the top of the archive, as the
#                             homebrew stores install it (ProsperoStore, RecompStore)
#   build/ps5/SpeedBreaker-<version>-ps5-third-party-source.tar.xz
#                             (third-party-source.sh)
#
#   port/ps5/tools/package-release.sh [build dir]        (default build/ps5)
#
# It refuses a build of uncommitted changes, and one whose
# sce_sys/param.json contentVersion is not the committed one.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build/ps5}" && pwd)"
TITLE_ID="${TITLE_ID:-PPSA90505}"
APP="$BUILD/app/$TITLE_ID"
[ -f "$APP/eboot.bin" ] && [ -f "$APP/VERSION" ] || { echo "no app folder in $APP (port/ps5/tools/link.sh)" >&2; exit 1; }
case "$(cat "$APP/VERSION")" in
    *-dirty*|*unknown*) echo "$(cat "$APP/VERSION"): build a commit, not uncommitted changes" >&2; exit 1 ;;
esac
cmp -s "$APP/sce_sys/param.json" "$ROOT/port/ps5/sce_sys/param.json" ||
    { echo "$APP/sce_sys/param.json is not port/ps5/sce_sys/param.json: link again" >&2; exit 1; }
ZIP="$ROOT/build/ps5/$TITLE_ID.zip"
rm -f "$ZIP"
(cd "$BUILD/app" && find "$TITLE_ID" -name .DS_Store -prune -o -type f -print | LC_ALL=C sort | zip -q -X -@ "$ZIP")
echo "$ZIP $(shasum -a 256 "$ZIP" | cut -d' ' -f1)"
"$ROOT/port/ps5/tools/third-party-source.sh"
echo "contentVersion $(sed -n 's/.*"contentVersion": *"\([^"]*\)".*/\1/p' "$APP/sce_sys/param.json"), $(cat "$APP/VERSION")"
