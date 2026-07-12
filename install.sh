#!/bin/sh
# tinyjs installer.
#
#   curl -fsSL https://raw.githubusercontent.com/tarwin/tinyjsapp/main/install.sh | sh
#
# Downloads the latest release (or TINYJS_VERSION=vX.Y.Z) into ~/.tinyjs
# and symlinks the `tinyjs` CLI onto your PATH.
set -e

REPO="tarwin/tinyjsapp"
INSTALL_DIR="${TINYJS_HOME:-$HOME/.tinyjs}"
VERSION="${TINYJS_VERSION:-latest}"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "tinyjs currently supports macOS only" >&2
  exit 1
fi

case "$(uname -m)" in
  arm64)  ARCH=arm64 ;;
  x86_64) ARCH=x86_64 ;;
  *) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

if [ "$VERSION" = "latest" ]; then
  VERSION=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
    grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"\([^"]*\)".*/\1/')
  [ -n "$VERSION" ] || { echo "could not determine latest release" >&2; exit 1; }
fi

ASSET="tinyjs-macos-$ARCH.tar.gz"
URL="https://github.com/$REPO/releases/download/$VERSION/$ASSET"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "==> downloading tinyjs $VERSION ($ARCH)"
curl -fsSL -o "$TMP/$ASSET" "$URL"

echo "==> verifying checksum"
curl -fsSL -o "$TMP/checksums.txt" \
  "https://github.com/$REPO/releases/download/$VERSION/checksums.txt"
(cd "$TMP" && grep " $ASSET\$" checksums.txt | shasum -a 256 -c - > /dev/null) ||
  { echo "checksum verification FAILED" >&2; exit 1; }

echo "==> installing to $INSTALL_DIR"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
tar -xzf "$TMP/$ASSET" -C "$INSTALL_DIR" --strip-components 1

# Clear quarantine on the binaries (curl doesn't set it, but belt & braces).
xattr -dr com.apple.quarantine "$INSTALL_DIR" 2>/dev/null || true

# Symlink onto PATH: prefer /usr/local/bin if writable, else ~/.local/bin.
if [ -w /usr/local/bin ]; then
  BIN_DIR=/usr/local/bin
else
  BIN_DIR="$HOME/.local/bin"
  mkdir -p "$BIN_DIR"
fi
ln -sf "$INSTALL_DIR/tinyjs" "$BIN_DIR/tinyjs"

echo "==> installed: $("$INSTALL_DIR/tinyjs" version 2>/dev/null || echo tinyjs $VERSION)"
echo "    $BIN_DIR/tinyjs -> $INSTALL_DIR/tinyjs"
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) echo ""
     echo "NOTE: $BIN_DIR is not on your PATH. Add this to your shell profile:"
     echo "  export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac
echo ""
echo "get started:  tinyjs new myapp && cd myapp && tinyjs dev"
