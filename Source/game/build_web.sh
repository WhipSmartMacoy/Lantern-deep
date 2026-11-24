#!/bin/sh
set -e
cd "$(dirname "$0")"
python3 tools/gen_art.py src/art_data.h
clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -Wno-unused-function -Wno-unused-variable \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,-z,stack-size=262144 -o build/lanterndeep.wasm plat/wasm_main.c
ls -la build/lanterndeep.wasm
python3 - <<'PY'
import base64
t = open('web/template.html').read()
w = base64.b64encode(open('build/lanterndeep.wasm','rb').read()).decode()
open('build/index.html','w').write(t.replace('__WASM__', w))
PY
ls -la build/index.html
