#!/usr/bin/env python3
"""Sets TuxAPE's Shaker screenshots beside photos of real machines.

The Logon System Shaker asks the emulator for a screenshot of each test
screen (see `tuxape-headless --csl`). https://shaker.logonsystem.eu keeps a
photo of every one of those screens taken on real hardware. This script
downloads the photos that match a folder of screenshots and builds contact
sheets, emulator on the left, real machine on the right, for a human to
look through. Photos cannot be compared automatically: they are framed and
lit differently from one to the next.

    tools/shaker-compare.py SCREENSHOT_DIR OUTPUT_DIR [--crtc N] [--tests A1,A2]
                            [--first-only] [--per-sheet N]

Needs Pillow. Reference photos are cached in tests/roms/shaker/reference/.
"""

import argparse
import json
import sys
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw

SITE = "https://shaker.logonsystem.eu"
ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "tests" / "roms" / "shaker" / "reference"
PAIR_WIDTH = 560  # width of each picture of a pair, in pixels
LABEL_HEIGHT = 18


def fetch(url, path):
    """Downloads url to path unless it is already there. Returns False if the site has no such file."""
    if path.exists() and path.stat().st_size > 0:
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
    except OSError:
        return False
    path.write_bytes(data)
    return True


def catalogue():
    path = CACHE / "tests.json"
    if not fetch(f"{SITE}/api/tests", path):
        sys.exit("cannot download the test catalogue")
    return json.loads(path.read_text())


def reference_name(test, sub, crtc):
    parts = [test["id"], f"CRTC{crtc}"]
    if sub.get("subfolder"):
        parts.append(sub["subfolder"])
    parts.append(sub["subTest"])
    return "_".join(parts)


def fit(image, width):
    height = round(image.height * width / image.width)
    return image.convert("RGB").resize((width, height), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("screenshots", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--crtc", type=int, default=0)
    parser.add_argument("--tests", help="comma-separated test ids, e.g. A1,A2 (default: all)")
    parser.add_argument("--first-only", action="store_true", help="only the first screen of each test")
    parser.add_argument("--per-sheet", type=int, default=4)
    parser.add_argument("--prefix", default="TUXAPE")
    args = parser.parse_args()

    wanted = set(args.tests.split(",")) if args.tests else None
    pairs = []
    missing = []
    for test in catalogue():
        if wanted and test["id"] not in wanted:
            continue
        subs = [s for s in test["subtests"] if args.crtc in s.get("crtcs", [])]
        if args.first_only:
            subs = subs[:1]
        for sub in subs:
            code = (sub.get("hex") or "").upper()
            mine = args.screenshots / f"{args.prefix}_{args.crtc}_{code}.png"
            name = reference_name(test, sub, args.crtc)
            if not mine.exists():
                missing.append(f"{name} (code {code})")
                continue
            photo = CACHE / "cpc" / f"{name}.webp"
            if not fetch(f"{SITE}/images/cpc/CPC/{name}.webp", photo):
                photo = None
            pairs.append((f"{name}  code {code}  {test['name']}", mine, photo))

    args.output.mkdir(parents=True, exist_ok=True)
    sheets = 0
    for start in range(0, len(pairs), args.per_sheet):
        group = pairs[start : start + args.per_sheet]
        rows = []
        for label, mine, photo in group:
            left = fit(Image.open(mine), PAIR_WIDTH)
            right = fit(Image.open(photo), PAIR_WIDTH) if photo else Image.new("RGB", left.size, (60, 0, 0))
            height = max(left.height, right.height) + LABEL_HEIGHT
            row = Image.new("RGB", (PAIR_WIDTH * 2 + 4, height), (40, 40, 40))
            ImageDraw.Draw(row).text((4, 3), label + ("" if photo else "   [no reference photo]"), fill=(255, 255, 0))
            row.paste(left, (0, LABEL_HEIGHT))
            row.paste(right, (PAIR_WIDTH + 4, LABEL_HEIGHT))
            rows.append(row)
        sheet = Image.new("RGB", (PAIR_WIDTH * 2 + 4, sum(r.height for r in rows)), (0, 0, 0))
        y = 0
        for row in rows:
            sheet.paste(row, (0, y))
            y += row.height
        sheets += 1
        sheet.save(args.output / f"sheet_{args.crtc}_{sheets:03d}.png")

    print(f"{len(pairs)} screens in {sheets} sheet(s) written to {args.output}")
    if missing:
        print(f"{len(missing)} screens of the catalogue have no screenshot, e.g. " + ", ".join(missing[:5]))


if __name__ == "__main__":
    main()
