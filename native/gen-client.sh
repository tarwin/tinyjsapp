#!/bin/sh
# Embed runtime/tiny.js into the launcher as tiny_client.h (generated; not
# committed). Run before compiling the launcher.
cd "$(dirname "$0")/.."
python3 - <<'EOF'
js = open('runtime/tiny.js').read()
assert ')TINYJS' not in js, 'raw-string delimiter collision'
with open('native/tiny_client.h', 'w') as f:
    f.write('// GENERATED from runtime/tiny.js by native/gen-client.sh — do not edit.\n')
    f.write('static const char TINY_CLIENT_JS[] = R"TINYJS(' + js + ')TINYJS";\n')
EOF
