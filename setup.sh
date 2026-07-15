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

# Non-weak frameworks (shared); ScreenCaptureKit + FoundationModels are
# weak-linked per-linker below. macOS 14 floor throughout — weak-linked
# frameworks activate only on newer OSes via @available guards.
FW="-framework WebKit -framework AppKit -framework Carbon -framework UserNotifications -framework AVFoundation -framework ServiceManagement -framework IOKit -framework Quartz -framework Vision -framework QuickLookThumbnailing -framework Security -framework LocalAuthentication -framework MediaPlayer -framework CoreMedia -framework CoreWLAN"
MIN_OS="-mmacosx-version-min=14.0"

if [ "${TINYJS_AI:-0}" = "1" ]; then
  # Opt-in on-device AI (tiny.app.ai) via Apple's FoundationModels. Requires
  # the macOS 26 SDK + swiftc, but the binary keeps the macOS 14 floor and
  # weak-links FoundationModels, so it still launches on macOS 14+ (AI just
  # reports 'unsupported' there). Two-step: compile each object, then link
  # with swiftc so the Swift runtime is pulled in. swiftc takes the floor via
  # -target and needs -Xlinker to pass -weak_framework to the linker.
  echo "==> (TINYJS_AI=1) building with FoundationModels"
  SWIFT_TARGET="$(uname -m)-apple-macos14.0"
  c++ -std=c++17 -c -x objective-c++ -DTINYJS_AI $MIN_OS -Inative/include \
    native/launcher.cc -o /tmp/tinyjs-launcher.o
  swiftc -parse-as-library -target "$SWIFT_TARGET" \
    -c native/tiny_ai.swift -o /tmp/tinyjs-ai.o
  swiftc /tmp/tinyjs-launcher.o /tmp/tinyjs-ai.o -o native/launcher -lc++ \
    -target "$SWIFT_TARGET" $FW \
    -Xlinker -weak_framework -Xlinker ScreenCaptureKit \
    -Xlinker -weak_framework -Xlinker FoundationModels -ldl
else
  c++ -std=c++17 -x objective-c++ $MIN_OS -Inative/include native/launcher.cc \
    -o native/launcher $FW -weak_framework ScreenCaptureKit -ldl
fi

codesign --force --sign - native/launcher 2>/dev/null || true

echo "==> done"
./bin/tjs --version
echo "try:  ./tinyjs new hello && cd hello && ../tinyjs dev"
