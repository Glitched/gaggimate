#!/usr/bin/env -S uv run --script
# /// script
# dependencies = ["pillow", "resvg-py"]
# ///
"""Regenerate the device UI's icon bitmaps from Phosphor Icons.

The mapping below is the source of truth for which Phosphor glyph stands in for
each bitmap in eez-ui/gaggimate.eez-project. For every entry this script
rasterizes the Phosphor SVG at the bitmap's existing pixel size (so no layout
changes), writes the PNG back into the project as base64, and regenerates the
matching src/display/ui/default/eez/images/ui_image_<name>.c with
scripts/lvgl_img.py. Symbol names are unchanged, so screens.c is untouched.

All icons are alpha masks recolored at runtime, which is why a black-on-
transparent raster of any monochrome SVG works. Weight is Regular throughout;
the `scale` column shrinks a glyph inside its box (the +/- buttons are
deliberately small next to the value they adjust).

    scripts/phosphor_icons.py                 # downloads @phosphor-icons/core via npm pack
    scripts/phosphor_icons.py --assets <dir>  # use an extracted package/assets directory

Phosphor Icons are MIT licensed (https://phosphoricons.com).
"""

import argparse
import base64
import glob
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile

import resvg_py
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROJECT = os.path.join(ROOT, "eez-ui", "gaggimate.eez-project")
IMAGES = os.path.join(ROOT, "src", "display", "ui", "default", "eez", "images")
WEIGHT = "regular"

# bitmap name in the EEZ project -> (phosphor icon, scale of the glyph within its box)
ICONS = {
    "angle-down-40x40": ("caret-down", 1.0),
    "angle-left-40x40": ("caret-left", 1.0),
    "angle-right-40x40": ("caret-right", 1.0),
    "angle-up-40x40": ("caret-up", 1.0),
    "bluetooth-alt-20x20": ("bluetooth", 1.0),
    "wifi-20x20": ("wifi-high", 1.0),
    "refresh-20x20": ("arrows-clockwise", 1.0),
    "check-40x40": ("check", 1.0),
    "clock-40x40": ("clock", 1.0),
    "clock-future-past-40x40": ("clock-counter-clockwise", 1.0),
    "info-40x40": ("info", 1.0),
    "disk-30x30": ("floppy-disk", 1.0),
    "floppy-disks-30x30": ("floppy-disk-back", 1.0),
    "dropdown-bar-40x40": ("list", 1.0),  # Brew screen: open the profile list
    "list": ("list-bullets", 1.0),  # Profile screen: list view (kept distinct from the above)
    "gallery": ("squares-four", 1.0),  # Profile screen: gallery view
    "equality-40x40": ("scales", 1.0),
    "flowmeter": ("waves", 1.0),
    "tachometer-fast-40x40": ("gauge", 1.0),
    "thermometer-half-40x40": ("thermometer-simple", 1.0),
    "minus-small-40x40": ("minus", 0.67),
    "plus-small-40x40": ("plus", 0.67),
    "pause-40x40": ("pause", 1.0),
    "play-40x40": ("play", 1.0),
    "power-40x40": ("power", 1.0),
    "settings-40x40": ("gear-fine", 1.0),
    "settings-80x80": ("gear-fine", 1.0),
    "coffee-bean-80x80": ("coffee-bean", 1.0),
    "mug-hot-alt-80x80": ("coffee", 1.0),
    "raindrops-80x80": ("drop", 1.0),
    "wind-40x40": ("wind", 1.0),
    "wind-80x80": ("wind", 1.0),
    "tap-60x60": ("hand-tap", 1.0),
}


def fetch_assets(workdir):
    subprocess.run(["npm", "pack", "@phosphor-icons/core", "--pack-destination", workdir], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    (tgz,) = glob.glob(os.path.join(workdir, "phosphor-icons-core-*.tgz"))
    with tarfile.open(tgz) as tar:
        tar.extractall(workdir)
    return os.path.join(workdir, "package", "assets")


def rasterize(assets, icon, size, scale):
    fname = "%s.svg" % icon if WEIGHT == "regular" else "%s-%s.svg" % (icon, WEIGHT)
    with open(os.path.join(assets, WEIGHT, fname)) as f:
        svg = f.read().replace("currentColor", "#000")
    px = max(1, round(size * scale))
    glyph = Image.open(io.BytesIO(resvg_py.svg_to_bytes(svg_string=svg, width=px, height=px))).convert("RGBA")
    if px == size:
        return glyph
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(glyph, ((size - px) // 2, (size - px) // 2), glyph)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", help="extracted @phosphor-icons/core package/assets directory")
    args = ap.parse_args()

    sys.path.insert(0, HERE)
    import lvgl_img  # noqa: E402  (scripts/lvgl_img.py)

    with open(PROJECT) as f:
        text = f.read()
    project = json.loads(text)
    by_name = {b["name"]: b for b in project["bitmaps"]}

    with tempfile.TemporaryDirectory() as tmp:
        assets = args.assets or fetch_assets(tmp)
        for name, (icon, scale) in ICONS.items():
            bitmap = by_name[name]
            old_b64 = bitmap["image"]
            size = Image.open(io.BytesIO(base64.b64decode(old_b64.split(",", 1)[1]))).size
            if size[0] != size[1]:
                sys.exit("%s is %dx%d; this script only handles square icons" % (name, *size))
            png_path = os.path.join(tmp, "%s.png" % name)
            rasterize(assets, icon, size[0], scale).save(png_path)
            with open(png_path, "rb") as f:
                new_b64 = "data:image/png;base64," + base64.b64encode(f.read()).decode()
            if text.count(old_b64) != 1:
                sys.exit("bitmap %s: its image data is not unique in the project" % name)
            text = text.replace(old_b64, new_b64)

            sym = "img_" + name.replace("-", "_")
            c_path = os.path.join(IMAGES, "ui_image_%s.c" % name.replace("-", "_"))
            if not os.path.exists(c_path):
                sys.exit("expected generated file missing: %s" % c_path)
            with open(c_path, "w") as f:
                f.write(lvgl_img.convert(png_path, sym))
            print("%-26s <- %-24s %dpx x%.2f" % (name, icon, size[0], scale))

    json.loads(text)  # still valid
    with open(PROJECT, "w") as f:
        f.write(text)
    print("updated %s and %d image files" % (os.path.relpath(PROJECT, ROOT), len(ICONS)))


if __name__ == "__main__":
    main()
