#!/usr/bin/env bash
# Makes dist/: TuxAPE ready to run, with its folders beside it. The build
# of the program does it each time (tools/update-dist.cmake says what goes
# there); this configures the project if it is not yet, builds the program
# and brings dist/ up to date, even if the program has not changed.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ ! -f "$root/build/CMakeCache.txt" ]; then
    (cd "$root" && cmake --preset dev)
fi
cmake --build "$root/build" -j"$(nproc)" --target dist

echo "TuxAPE is in $root/dist: start it with $root/dist/tuxape"
