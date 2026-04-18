#!/usr/bin/env python3
"""
upscale_sprites.py — Upscale OpenParsec background sprite PNGs by 4x.

Usage:
    python3 tool_src/upscale_sprites.py [--dry-run] [--scale N]

Options:
    --dry-run   Show what would be done without writing files
    --scale N   Upscale factor (default: 4, max capped so output <= 2048px)

By default operates on: openparsec-assets/Images/Image_2D_*.png
Output is written back in-place (originals are overwritten).
Back up the Images/ directory before running if you want to keep the originals.

Uses Pillow LANCZOS (high-quality bicubic-class resampling).
For AI upscaling, install Real-ESRGAN (https://github.com/xinntao/Real-ESRGAN)
and use its CLI instead — the power-of-2 dimensions produced here are compatible.
"""

import sys
import os
import glob
import argparse
from PIL import Image

MAX_TEX_SIZE = 2048  # OpenParsec GPU texture size limit


def upscale_image(path: str, scale: int, dry_run: bool) -> tuple[int, int, int, int]:
    im = Image.open(path)
    w, h = im.size

    # Cap so neither dimension exceeds MAX_TEX_SIZE
    actual_scale = scale
    while actual_scale > 1 and (w * actual_scale > MAX_TEX_SIZE or h * actual_scale > MAX_TEX_SIZE):
        actual_scale //= 2

    if actual_scale <= 1:
        print(f"  SKIP  {os.path.basename(path)}  ({w}x{h} already at limit)")
        return w, h, w, h

    nw, nh = w * actual_scale, h * actual_scale
    print(f"  {w:4}x{h:<4} → {nw:4}x{nh:<4}  {os.path.basename(path)}"
          + (f"  [scale={actual_scale} (capped)]" if actual_scale != scale else ""))

    if not dry_run:
        upscaled = im.resize((nw, nh), Image.LANCZOS)
        upscaled.save(path, "PNG", optimize=False)

    return w, h, nw, nh


def main():
    parser = argparse.ArgumentParser(description="Upscale OpenParsec sprite PNGs")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would be done without writing files")
    parser.add_argument("--scale", type=int, default=4,
                        help="Upscale factor (default: 4)")
    parser.add_argument("--images-dir", default="openparsec-assets/Images",
                        help="Path to Images/ directory")
    args = parser.parse_args()

    if args.dry_run:
        print("DRY RUN — no files will be written\n")

    pattern = os.path.join(args.images_dir, "Image_2D_*.png")
    paths = sorted(glob.glob(pattern))

    if not paths:
        print(f"No PNG files found matching: {pattern}")
        sys.exit(1)

    print(f"Found {len(paths)} PNG files in {args.images_dir}")
    print(f"Scale factor: {args.scale}x  (capped to {MAX_TEX_SIZE}px max)\n")

    total = skipped = 0
    for p in paths:
        w, h, nw, nh = upscale_image(p, args.scale, args.dry_run)
        if (nw, nh) != (w, h):
            total += 1
        else:
            skipped += 1

    print(f"\nDone. Upscaled: {total}  Skipped (already at limit): {skipped}")
    if args.dry_run:
        print("(dry run — no files written)")


if __name__ == "__main__":
    main()
