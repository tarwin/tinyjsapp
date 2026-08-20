#!/bin/sh
# Morning demo: a real tinyjs window driven by a scriptc-compiled native
# backend — no tjs, no QuickJS in the backend process.
#
#   ./run-spike.sh          window stays until you close it (or ^C here)
#   ./run-spike.sh --auto   window closes itself after ~9s (the overnight run)
#
# Needs: Node 24+ on PATH, clang (Xcode CLT). First run npm-installs scriptc
# into this directory (~40MB, local only).
#
# tsconfig.json here is load-bearing: without one, scriptc's tsc pass walks
# up and adopts the first tsconfig it finds — a stray ~/tsconfig.json made
# it index the entire home directory (minutes, looked like a hang).
set -e
cd "$(dirname "$0")"
REPO="$(cd .. && pwd)"

if [ ! -x node_modules/.bin/scriptc ]; then
  echo "installing scriptc locally..."
  npm install --save-dev scriptc >/dev/null
fi

echo "compiling spike-backend.ts -> native binary..."
node_modules/.bin/scriptc build spike-backend.ts -o spike-backend
ls -la spike-backend

MODE="--stay"
[ "$1" = "--auto" ] && MODE=""
exec ./spike-backend "$REPO/native/launcher-macos" "$PWD/spike-page.html" $MODE
