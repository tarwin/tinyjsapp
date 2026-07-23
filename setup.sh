#!/bin/sh
# Bootstrap tinyjs from a source checkout: download the txiki.js runtime
# and compile the native launcher. Run once after cloning (the curl
# installer ships these prebuilt; this script is for developing tinyjs
# itself). Supports macOS and Linux (see README: Portability).
set -e
cd "$(dirname "$0")"

TJS_VERSION="${TJS_VERSION:-v26.6.0}"
REPO="tarwin/tinyjsapp"

OS="$(uname -s)"
case "$(uname -m)" in
  arm64|aarch64) TJS_ARCH=arm64 ;;
  x86_64)        TJS_ARCH=x86_64 ;;
  *) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

# ---------------------------------------------------------------- Linux ----
if [ "$OS" = "Linux" ]; then
  # Build deps: a C++ toolchain + GTK3/WebKitGTK dev packages (AppIndicator
  # optional — tray support). Debian/Ubuntu:
  #   sudo apt install build-essential pkg-config libgtk-3-dev \
  #        libwebkit2gtk-4.1-dev libayatana-appindicator3-dev
  for p in gtk+-3.0 webkit2gtk-4.1; do
    pkg-config --exists "$p" 2>/dev/null || {
      echo "missing dev package: $p" >&2
      echo "Debian/Ubuntu: sudo apt install build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev libayatana-appindicator3-dev" >&2
      exit 1
    }
  done

  if [ ! -x bin/tjs ]; then
    # txiki.js publishes no Linux binaries — grab the one from our own
    # releases (built by CI), or build from source (TJS_BUILD=1 forces it).
    mkdir -p bin
    GOT=""
    if [ "${TJS_BUILD:-0}" != "1" ]; then
      TJS_REL="${TINYJS_TJS_RELEASE:-latest}"
      if [ "$TJS_REL" = "latest" ]; then
        TJS_URL="https://github.com/$REPO/releases/latest/download/tjs-linux-$TJS_ARCH.gz"
      else
        TJS_URL="https://github.com/$REPO/releases/download/$TJS_REL/tjs-linux-$TJS_ARCH.gz"
      fi
      echo "==> downloading tjs (linux-$TJS_ARCH) from $REPO releases"
      if curl -fsSL -o /tmp/tjs-$$.gz "$TJS_URL" 2>/dev/null; then
        gunzip -c /tmp/tjs-$$.gz > bin/tjs && rm -f /tmp/tjs-$$.gz && GOT=1
      else
        echo "    (no prebuilt tjs in the latest release — building from source)"
      fi
    fi
    if [ -z "$GOT" ]; then
      command -v cmake >/dev/null || { echo "building txiki.js needs cmake (sudo apt install cmake ninja-build)" >&2; exit 1; }
      echo "==> building txiki.js $TJS_VERSION from source (a few minutes)"
      rm -rf /tmp/txiki-src-$$
      git clone --depth 1 --branch "$TJS_VERSION" --recurse-submodules --shallow-submodules -j4 \
        https://github.com/saghul/txiki.js /tmp/txiki-src-$$
      GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN=Ninja
      cmake -S /tmp/txiki-src-$$ -B /tmp/txiki-src-$$/build -DCMAKE_BUILD_TYPE=Release -G "$GEN"
      cmake --build /tmp/txiki-src-$$/build --target tjs -j"$(nproc)"
      cp /tmp/txiki-src-$$/build/tjs bin/tjs
      rm -rf /tmp/txiki-src-$$
    fi
    chmod +x bin/tjs
  fi

  echo "==> compiling launcher"
  ./native/gen-client.sh

  # Tray icons need an AppIndicator library (ayatana preferred); without one
  # the launcher compiles fine and tiny.tray reports 'unsupported'.
  IND_PKG=""
  for p in ayatana-appindicator3-0.1 appindicator3-0.1; do
    pkg-config --exists "$p" 2>/dev/null && { IND_PKG="$p"; break; }
  done
  EXTRA_DEFS=""
  [ -n "$IND_PKG" ] && EXTRA_DEFS="-DTINYJS_APPINDICATOR"
  [ -z "$IND_PKG" ] && echo "    (no appindicator dev package — tray support disabled)"

  # X11/XTest power global hotkeys + synthetic keystrokes on X11/XWayland
  # sessions (no Wayland-native route yet; see TODO-linux.md).
  XT_PKGS=""
  pkg-config --exists x11 xtst 2>/dev/null && { XT_PKGS="x11 xtst"; EXTRA_DEFS="$EXTRA_DEFS -DTINYJS_X11"; }

  # shellcheck disable=SC2086
  c++ -std=c++17 -O2 native/launcher-linux.cc -o native/launcher-linux \
    $EXTRA_DEFS \
    $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1 $IND_PKG $XT_PKGS) -ldl

  echo "==> done"
  ./bin/tjs --version
  echo "try:  ./tinyjs new hello && cd hello && ../tinyjs dev"
  exit 0
fi

# ---------------------------------------------------------------- macOS ----
if [ "$OS" != "Darwin" ]; then
  echo "tinyjs supports macOS and Linux (see README: Portability)" >&2
  exit 1
fi

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
FW="-framework WebKit -framework AppKit -framework Carbon -framework UserNotifications -framework AVFoundation -framework ServiceManagement -framework IOKit -framework Quartz -framework Vision -framework QuickLookThumbnailing -framework Security -framework LocalAuthentication -framework MediaPlayer -framework CoreMedia -framework CoreWLAN -framework CoreAudio -framework AudioToolbox"
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
