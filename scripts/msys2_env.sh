#!/usr/bin/env bash
# Source me in terminals that don’t already export these.
export MSYSTEM=${MSYSTEM:-MINGW64}
export CHERE_INVOKING=1

# Let .env override RACK_DIR/RACK_SDK if present.
if [ -f ./.env ]; then set -a; . ./.env; set +a; fi
[ -z "$RACK_DIR" ] && [ -n "$RACK_SDK" ] && export RACK_DIR="$RACK_SDK"

# Ensure MSYS2 tools are first on PATH (adjust if you keep them elsewhere).
case ":$PATH:" in
  *":/c/msys64/usr/bin:"*) : ;;
  *) export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH" ;;
esac
