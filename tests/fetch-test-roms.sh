#!/usr/bin/env bash
# Downloads the third-party test programs used by the test suite into
# tests/roms/. They are not part of this repository.
#
#   ./tests/fetch-test-roms.sh            Z80 exercisers (a few hundred kB)
#   ./tests/fetch-test-roms.sh --sst      also the per-instruction JSON tests
#                                         (about 1600 files, 1.3 GB)
#   ./tests/fetch-test-roms.sh --acid     also Kevin Thacker's CPC hardware
#                                         tests (50 MB unpacked)
#   ./tests/fetch-test-roms.sh --shaker   also the Logon System "Shaker"
#
# Several options can be given together.
set -euo pipefail

want_sst=0 want_acid=0 want_shaker=0
for option in "$@"; do
    case "$option" in
    --sst) want_sst=1 ;;
    --acid) want_acid=1 ;;
    --shaker) want_shaker=1 ;;
    *) echo "unknown option: $option" >&2; exit 2 ;;
    esac
done

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

if [ "$want_acid" = 1 ] && [ ! -d "$dest/acid" ]; then
    # The "acid tests" of the Arnold emulator: programs that check one piece
    # of hardware each and print PASS or FAIL, with their sources.
    fetch http://www.cpctech.org.uk/test.zip "$dest/acid.zip"
    unzip -q -o "$dest/acid.zip" -d "$dest/acid-unpack"
    mv "$dest/acid-unpack/test" "$dest/acid"
    rmdir "$dest/acid-unpack"
    rm "$dest/acid.zip"
fi

if [ "$want_shaker" = 1 ] && [ ! -d "$dest/shaker" ]; then
    # Longshot's test disc for the CRTC and Gate Array, with the scripts
    # that drive it automatically. Pictures of what each test shows on real
    # machines are at https://shaker.logonsystem.eu/tests
    fetch https://shaker.logonsystem.eu/Shaker_CSL.zip "$dest/shaker.zip"
    unzip -q -o "$dest/shaker.zip" -d "$dest/shaker-unpack"
    mv "$dest/shaker-unpack/Shaker_CSL" "$dest/shaker"
    rmdir "$dest/shaker-unpack"
    rm "$dest/shaker.zip"
fi

if [ "$want_sst" = 1 ]; then
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
