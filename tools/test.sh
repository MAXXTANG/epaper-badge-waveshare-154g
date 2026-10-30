#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/.dev"
node "$root/tools/test_web.mjs"
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
  "$root/tools/test_receiver.cpp" -o "$root/.dev/test_receiver"
"$root/.dev/test_receiver"
