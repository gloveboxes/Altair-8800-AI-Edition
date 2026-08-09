#!/usr/bin/env sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir"

dccmake
mv -f build/DOCTOR.COM .
rm -rf build

dccmake \
	dcc-input=docgen.c,isamdb.c \
	dcc-output=DOCGEN \
	dcc-build-dir=build-docgen
mv -f build-docgen/DOCGEN.COM .
rm -rf build-docgen