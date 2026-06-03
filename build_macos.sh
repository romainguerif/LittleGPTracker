#!/usr/bin/env bash
# Native macOS (Apple Silicon / Intel) build for Little Piggy Tracker.
# Requires: brew install sdl2 pkg-config  + a python3 with Pillow (PIL).
#
# Usage:
#   ./build_macos.sh          # build
#   ./build_macos.sh clean    # clean MACOS build artifacts
#   ./build_macos.sh run      # build then launch
#
# Produces: projects/lgpt.mac (run it from inside projects/).
set -e

cd "$(dirname "$0")/projects"

# The font generator (mkfont.py) needs a python3 with a working Pillow.
# The system /Library/Frameworks python3 ships a broken PIL, so make sure a
# good python3 (Homebrew or /usr/bin) is found first.
export PATH="/opt/homebrew/bin:/usr/bin:$PATH"

case "$1" in
  clean)
    make PLATFORM=MACOS clean
    rm -f lgpt.mac
    ;;
  run)
    make PLATFORM=MACOS
    ./lgpt.mac
    ;;
  *)
    make PLATFORM=MACOS
    echo ""
    echo "Built: projects/lgpt.mac"
    echo "Run with:  (cd projects && ./lgpt.mac)"
    ;;
esac
