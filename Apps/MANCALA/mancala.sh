#!/usr/bin/env bash
#
# mancala.sh - Build MANC89.COM (dcc C89 Mancala) and copy it next to the source.
#
# The dcc tool chain builds from its own repo root, so this script copies
# manc89.c into the dcc repo, builds it with a raised C stack (the alpha-beta
# search recurses to depth 5 and the default 512-byte stack silently collides
# with the heap), then copies the resulting MANC89.COM back into this folder
# and removes the temporary source from the dcc repo.
#
# Usage:
#   ./mancala.sh [peep|nopeep]
#
# Environment overrides:
#   DCC_REPO        path to the dcc repo   (default: $HOME/GitHub/dcc)
#   NTVCM_REPO      path to the ntvcm repo (default: $HOME/GitHub/ntvcm)
#   DCC_STACK_SIZE  C stack size in bytes  (default: 8192)

set -euo pipefail

APP="manc89"
COM="MANC89.COM"

# Directory containing this script (and manc89.c / the output COM).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DCC_REPO="${DCC_REPO:-$HOME/GitHub/dcc}"
NTVCM_REPO="${NTVCM_REPO:-$HOME/GitHub/ntvcm}"
DCC_STACK_SIZE="${DCC_STACK_SIZE:-8192}"
MODE="${1:-peep}"

if [ ! -d "$DCC_REPO" ]; then
    echo "error: dcc repo not found at '$DCC_REPO' (set DCC_REPO)" >&2
    exit 1
fi
if [ ! -f "$SCRIPT_DIR/$APP.c" ]; then
    echo "error: source '$SCRIPT_DIR/$APP.c' not found" >&2
    exit 1
fi

echo "Building $COM from $APP.c (stack=$DCC_STACK_SIZE, mode=$MODE)..."

# Stage the source in the dcc repo and remove it again on exit.
cp "$SCRIPT_DIR/$APP.c" "$DCC_REPO/$APP.c"
trap 'rm -f "$DCC_REPO/$APP.c"' EXIT

(
    cd "$DCC_REPO"
    export PATH="$NTVCM_REPO:$DCC_REPO:$PATH"
    export DCC=./dcc DCCPEEP=./dccpeep DCCRTLSTRIP=./dccrtlstrip
    export DCC_STACK_SIZE
    ./ma.sh "$APP" "$MODE"
)

cp "$DCC_REPO/build/$COM" "$SCRIPT_DIR/$COM"
echo "Done: $SCRIPT_DIR/$COM"
