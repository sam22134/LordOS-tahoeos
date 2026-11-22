#!/usr/bin/env bash
# Install the MacTahoe icon theme from a pinned upstream revision.
# Run: sudo bash packaging/icons/install-mactahoe.sh
set -euo pipefail

UPSTREAM_REV=839848b9a8a38a92a6936e30c4abe35cc6f2546d
DEST="${DESTDIR:-}/usr/share/icons"
SOURCE_DIR="${MACTAHOE_SOURCE_DIR:-}"
TMP=""

cleanup() {
  [[ -z "$TMP" ]] || rm -rf -- "$TMP"
}
trap cleanup EXIT

if [[ -z "${DESTDIR:-}" && "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "Run as root, or set DESTDIR to a staging directory." >&2
  exit 1
fi

TMP="$(mktemp -d)"
if [[ -n "$SOURCE_DIR" ]]; then
  if [[ ! -f "$SOURCE_DIR/install.sh" || ! -f "$SOURCE_DIR/COPYING" || ! -f "$SOURCE_DIR/src/index.theme" ]]; then
    echo "The source directory is not a complete MacTahoe-icon-theme checkout." >&2
    exit 1
  fi
  # Work on a temporary copy: the optional cache-tool compatibility patch must not alter a user's checkout.
  mkdir -p "$TMP/source"
  cp -a "$SOURCE_DIR/." "$TMP/source/"
  SOURCE_DIR="$TMP/source"
else
  command -v curl >/dev/null 2>&1 || { echo "curl is required to download the pinned icon source." >&2; exit 1; }
  command -v tar >/dev/null 2>&1 || { echo "tar is required." >&2; exit 1; }
  ARCHIVE="$TMP/mactahoe-icons.tar.gz"
  URL="https://codeload.github.com/vinceliuice/MacTahoe-icon-theme/tar.gz/$UPSTREAM_REV"
  curl --fail --location --retry 3 --output "$ARCHIVE" "$URL"
  tar -xzf "$ARCHIVE" -C "$TMP"
  SOURCE_DIR="$(find "$TMP" -mindepth 1 -maxdepth 1 -type d -name 'MacTahoe-icon-theme-*' -print -quit)"
fi

if [[ -z "$SOURCE_DIR" || ! -f "$SOURCE_DIR/install.sh" || ! -f "$SOURCE_DIR/COPYING" || ! -f "$SOURCE_DIR/src/index.theme" ]]; then
  echo "The source directory is not a complete MacTahoe-icon-theme checkout." >&2
  exit 1
fi

mkdir -p "$DEST"

# The upstream installer refreshes icon caches when gtk-update-icon-cache is present.
# Keep staged/minimal LordOS builds working when that optional GTK utility is absent.
if ! command -v gtk-update-icon-cache >/dev/null 2>&1; then
  sed -i 's|^[[:space:]]*gtk-update-icon-cache "${THEME_DIR}"$|  : # gtk-update-icon-cache is optional|' "$SOURCE_DIR/install.sh"
fi

bash "$SOURCE_DIR/install.sh" --dest "$DEST" --name MacTahoe --theme blue

echo "Installed MacTahoe-blue (blue, light and dark variants) into $DEST"
echo "Upstream: https://github.com/vinceliuice/MacTahoe-icon-theme"
echo "Pinned revision: $UPSTREAM_REV"
