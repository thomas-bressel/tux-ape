#!/usr/bin/env bash
# Downloads the third-party test programs used by the test suite into
# tests/roms/. They are not part of this repository.
#
#   ./tests/fetch-test-roms.sh          Z80 exercisers (a few hundred kB)
#   ./tests/fetch-test-roms.sh --sst    also the per-instruction JSON tests
#                                       (about 1600 files, 1.3 GB)
set -euo pipefail

dest="$(cd "$(dirname "$0")" && pwd)/roms"
mkdir -p "$dest"

fetch() {  # url, output file
    [ -s "$2" ] || curl -fsSL --retry 3 -o "$2" "$1"
}

# Frank Cringle's instruction exercisers, as CP/M programs.
zex=https://raw.githubusercontent.com/superzazu/z80/master/roms
fetch "$zex/prelim.com" "$dest/prelim.com"
fetch "$zex/zexdoc.cim" "$dest/zexdoc.com"
fetch "$zex/zexall.cim" "$dest/zexall.com"

if [ "${1:-}" = "--sst" ]; then
    # https://github.com/SingleStepTests/z80 — 1000 cases per opcode, each
    # with the expected bus activity for every T-state.
    sst=https://raw.githubusercontent.com/SingleStepTests/z80/main/v1
    mkdir -p "$dest/sst"
    {
        for op in $(seq 0 255); do
            hex=$(printf '%02x' "$op")
            case "$hex" in cb | dd | ed | fd) continue ;; esac
            echo "$hex"
            echo "dd $hex"
            echo "fd $hex"
        done
        for op in $(seq 0 255); do
            hex=$(printf '%02x' "$op")
            echo "cb $hex"
            echo "dd cb __ $hex"
            echo "fd cb __ $hex"
        done
        for op in $(seq 64 127) $(seq 160 163) $(seq 168 171) $(seq 176 179) $(seq 184 187); do
            echo "ed $(printf '%02x' "$op")"
        done
    } | while read -r name; do
        echo "${name// /%20}.json"
    done | xargs -P 8 -I{} sh -c '
        out="$1/sst/$(printf "%s" "$2" | sed "s/%20/ /g")"
        [ -s "$out" ] || curl -fsSL --retry 3 -o "$out" "$3/$2" || echo "missing: $2" >&2
    ' _ "$dest" {} "$sst"
fi

echo "Test programs are in $dest"
