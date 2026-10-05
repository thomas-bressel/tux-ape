#!/usr/bin/env bash
# Downloads the reference documents the emulation is written from into
# reference/. They are not part of this repository: read their licences
# before passing them on.
set -euo pipefail

dest="$(cd "$(dirname "$0")/.." && pwd)/reference"
mkdir -p "$dest"

fetch() {  # url, output file
    [ -s "$2" ] || curl -fsSL --retry 3 -o "$2" "$1"
}

# "Amstrad CPC CRTC Compendium" by Longshot / Logon System (CC BY-NC-ND 4.0):
# the reference for the CRTC, the Gate Array and video timing. Check the
# site for a newer version than the one named here.
site=https://shaker.logonsystem.eu
fetch "$site/ACCC1.11-FR.pdf" "$dest/ACCC1.11-FR.pdf"
fetch "$site/ACCC1.11-EN.pdf" "$dest/ACCC1.11-EN.pdf"

# The standards for scripting emulators (CSL) and taking reference
# screenshots (SSM) that go with the Shaker test disc.
fetch "$site/Shaker_CSL/CSL-STANDARD-EN.pdf" "$dest/CSL-STANDARD-EN.pdf"
fetch "$site/Shaker_CSL/SSM-STANDARD-EN.pdf" "$dest/SSM-STANDARD-EN.pdf"

# Plain-text copies are handy for searching.
if command -v pdftotext >/dev/null; then
    [ -s "$dest/accc_fr.txt" ] || pdftotext -layout "$dest/ACCC1.11-FR.pdf" "$dest/accc_fr.txt"
    [ -s "$dest/accc_en.txt" ] || pdftotext -layout "$dest/ACCC1.11-EN.pdf" "$dest/accc_en.txt"
fi

echo "Reference documents are in $dest"
