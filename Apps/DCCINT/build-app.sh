#!/usr/bin/env sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir"

: "${DCC_DIR:?Set DCC_DIR to the dcc repository path}"

"$DCC_DIR/dccmake"
cp -f build/DCCINT.COM .