#!/bin/sh
# Bootstrap tinyjs from a source checkout: download the txiki.js runtime
# and compile the native launcher. Run once after cloning (the curl
# installer ships these prebuilt; this script is for developing tinyjs
# itself).
set -e
cd "$(dirname "$0")"

TJS_VERSION="${TJS_VERSION:-v26.6.0}"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "tinyjs currently supports macOS only (see README: Portability)" >&2
  exit 1
fi

case "$(uname -m)" in
  arm64)  TJS_ARCH=arm64 ;;
  x86_64) TJS_ARCH=x86_64 ;;
  *) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

if [ ! -x bin/tjs ]; then
  echo "==> downloading txiki.js $TJS_VERSION ($TJS_ARCH)"
  mkdir -p bin
  curl -fsSL -o /tmp/txiki-$$.zip \
    "https://github.com/saghul/txiki.js/releases/download/$TJS_VERSION/txiki-macos-$TJS_ARCH.zip"
  unzip -q -o /tmp/txiki-$$.zip -d /tmp/txiki-$$
  mv "/tmp/txiki-$$/txiki-macos-$TJS_ARCH/tjs" bin/tjs
  rm -rf /tmp/txiki-$$ /tmp/txiki-$$.zip
  chmod +x bin/tjs
fi

echo "==> compiling launcher"
./native/gen-client.sh
c++ -std=c++17 -x objective-c++ -Inative/include native/launcher.cc \
  -o native/launcher -framework WebKit -framework AppKit -framework Carbon -framework UserNotifications -framework AVFoundation -ldl

codesign --force --sign - native/launcher 2>/dev/null || true

echo "==> done"
./bin/tjs --version
echo "try:  ./tinyjs new hello && cd hello && ../tinyjs dev"
