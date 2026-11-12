#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MCP_SRC_DIR="$REPO_ROOT/altair_mcp_server"
MCP_BUILD_DIR="$MCP_SRC_DIR/build"

CLEAN=0
BUILD_TYPE="Release"

for arg in "$@"; do
    case "$arg" in
        --clean)
            CLEAN=1
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
        -h|--help)
            cat <<'EOF'
Usage: build-host-tools.sh [--clean] [--debug]

Build the MCP server:
  - altair_mcp_server/build/altair-cpm-mcp

Options:
  --clean   Remove the MCP build directory before configuring.
  --debug   Configure Debug builds instead of Release.
EOF
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 1
            ;;
    esac
done

if [[ $CLEAN -eq 1 ]]; then
    rm -rf "$MCP_BUILD_DIR"
fi

echo "Configuring altair_mcp_server ($BUILD_TYPE)..."
cmake -S "$MCP_SRC_DIR" -B "$MCP_BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "Building altair_mcp_server..."
cmake --build "$MCP_BUILD_DIR" -j

MCP_BIN="$MCP_BUILD_DIR/altair-cpm-mcp"

if [[ ! -x "$MCP_BIN" ]]; then
    echo "Build finished but binary not found at $MCP_BIN" >&2
    exit 1
fi

echo
echo "Built: $MCP_BIN"