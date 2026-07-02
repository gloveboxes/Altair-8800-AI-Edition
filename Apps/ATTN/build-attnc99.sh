#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
dcc_dir=${DCC_DIR:-"$HOME/GitHub/dcc"}
ntvcm_dir=${NTVCM_DIR:-"$HOME/GitHub/ntvcm"}

if [ ! -x "$dcc_dir/dcc" ] || [ ! -x "$dcc_dir/dccpeep" ] || [ ! -x "$dcc_dir/dccrtlstrip" ]; then
    echo "dcc toolchain not found in $dcc_dir" >&2
    echo "Set DCC_DIR to the dcc repository path." >&2
    exit 1
fi

if [ ! -x "$ntvcm_dir/ntvcm" ]; then
    echo "ntvcm not found in $ntvcm_dir" >&2
    echo "Set NTVCM_DIR to the ntvcm repository path." >&2
    exit 1
fi

if [ ! -x "$dcc_dir/ma.sh" ]; then
    echo "ma.sh not found or not executable in $dcc_dir" >&2
    exit 1
fi

export PATH="$ntvcm_dir:$dcc_dir:$PATH"
export DCC=./dcc
export DCCPEEP=./dccpeep
export DCCRTLSTRIP=./dccrtlstrip

cd "$dcc_dir"
./ma.sh "$script_dir/attnc99" peep
cp -f build/ATTNC99.COM "$script_dir/ATTNC99.COM"

ls -l "$script_dir/ATTNC99.COM"