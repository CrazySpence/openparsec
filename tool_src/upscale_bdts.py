#!/usr/bin/env python3
"""
upscale_bdts.py — Extract BDT sprite files from OpenParsec pscdata archives,
upscale them 4×, and write the results into a gamedata/ override folder.

The gamedata/ folder is checked before the packed .dat archives (via the
loose-file override added to sys_file.cpp), so files placed there
transparently replace the originals without repacking.

Usage (run from repo root):
    python3 tool_src/upscale_bdts.py [--dry-run] [--scale N] [--assets-dir PATH]

Output: <assets-dir>/gamedata/   (created if needed)

BDT sprite format:
  - Most BDT files are raw 8-bit palette-indexed pixel data (width × height bytes).
    Dimensions are stored in the control file (ctrl_h_h.dat etc.), not in the BDT file.
  - Some BDT files (sun, lightnings, fireballs, shields) have a PARBDT header that
    stores their own dimensions.
  - All use the 256-color palette from game.pal (256 × 3 RGB bytes).
  - Palette index 0 is the transparent colour.

This tool:
  1. Loads game.pal and all BDT files from pscdata0.dat
  2. Converts 8-bit indexed → RGBA, upscales with LANCZOS, re-quantises to palette
  3. Writes upscaled BDT files to gamedata/
  4. Writes updated ctrl_*.dat files to gamedata/ with the new bitmap dimensions
     (the control files must be updated so the game reads the right pixel count)
"""

import sys
import os
import re
import struct
import argparse
import glob
import numpy as np
from PIL import Image

# ---- Package format (gd_heads.h) ----
PACK_HDR_FMT  = '<16sIIII'   # signature[16], numitems, headersize, datasize, packsize
PACK_HDR_SIZE = struct.calcsize(PACK_HDR_FMT)   # 32 bytes
ITEM_FMT      = '<16sIIII'   # file[16], foffset, flength, fp, fcurpos
ITEM_SIZE     = struct.calcsize(ITEM_FMT)        # 32 bytes

# ---- BDT format (gd_heads.h) ----
BDT_SIG       = b'PARBDT'
BDT_HDR_FMT   = '<7sBii'     # signature[7], version(1), width(4), height(4) = 16 bytes
BDT_HDR_SIZE  = struct.calcsize(BDT_HDR_FMT)    # 16 bytes

MAX_TEX_SIZE  = 2048


# ── Package reader ────────────────────────────────────────────────────────────

def read_package(path: str) -> dict:
    """Parse a pscdata*.dat and return {filename: bytes} for every item."""
    with open(path, 'rb') as f:
        raw = f.read()
    sig, numitems, headersize, datasize, packsize = struct.unpack_from(PACK_HDR_FMT, raw, 0)
    if not sig.startswith(b'msh DataPackage'):
        raise ValueError(f"Bad signature in {path}: {sig!r}")
    items = {}
    for i in range(numitems):
        base = PACK_HDR_SIZE + i * ITEM_SIZE
        fname16, foffset, flength, fp_, fcurpos = struct.unpack_from(ITEM_FMT, raw, base)
        fname = fname16.rstrip(b'\x00').decode('latin-1')
        items[fname] = raw[foffset : foffset + flength]
    return items


# ── Palette ───────────────────────────────────────────────────────────────────

def load_palette(pal_bytes: bytes) -> list:
    """Build a 256-entry [(R,G,B,A)] list from raw 256×3-byte palette data."""
    assert len(pal_bytes) == 768, f"Expected 768-byte palette, got {len(pal_bytes)}"
    palette = []
    for i in range(256):
        r, g, b = pal_bytes[i*3], pal_bytes[i*3+1], pal_bytes[i*3+2]
        alpha = 0 if i == 0 else 255   # index 0 is transparent
        palette.append((r, g, b, alpha))
    return palette


def indices_to_rgba(indices: bytes, palette: list) -> bytes:
    """Convert raw 8-bit palette indices to a flat RGBA byte array (via numpy)."""
    pal_arr = np.array(palette, dtype=np.uint8)   # (256, 4) RGBA
    idx_arr = np.frombuffer(indices, dtype=np.uint8)
    return pal_arr[idx_arr].tobytes()


def rgba_to_indices(rgba_bytes: bytes, palette: list, w: int, h: int) -> bytes:
    """Re-quantise upscaled RGBA pixels back to the nearest 8-bit palette index (numpy)."""
    # pixels: (w*h, 4)
    pixels = np.frombuffer(rgba_bytes, dtype=np.uint8).reshape(-1, 4).astype(np.int32)
    alpha  = pixels[:, 3]

    # palette RGB values for indices 1-255 (skip 0, reserved for transparency)
    pal_rgb = np.array([[p[0], p[1], p[2]] for p in palette[1:]], dtype=np.int32)  # (255, 3)

    # Compute squared distance from each pixel RGB to each palette entry
    # pixels RGB: (N, 1, 3),  pal_rgb: (1, 255, 3)  →  (N, 255)
    rgb = pixels[:, :3][:, np.newaxis, :]   # (N, 1, 3)
    diff = rgb - pal_rgb[np.newaxis, :, :]  # (N, 255, 3)
    dist = (diff ** 2).sum(axis=2)          # (N, 255)
    nearest = dist.argmin(axis=1) + 1       # +1 because we skipped index 0

    # Transparent pixels → index 0
    nearest[alpha < 128] = 0
    return nearest.astype(np.uint8).tobytes()


# ── BDT read / write ──────────────────────────────────────────────────────────

def read_bdt_raw(data: bytes, ctrl_w: int, ctrl_h: int):
    """
    Read a BDT file and return (has_header, width, height, raw_8bit_pixels).
    ctrl_w / ctrl_h are the dimensions from the control file (-1 if not listed).
    """
    has_header = data[:6] == BDT_SIG
    if has_header:
        sig, version, w, h = struct.unpack_from(BDT_HDR_FMT, data, 0)
        pixels = data[BDT_HDR_SIZE : BDT_HDR_SIZE + w * h]
    else:
        if ctrl_w < 0 or ctrl_h < 0:
            return None   # no dimensions available anywhere
        w, h = ctrl_w, ctrl_h
        pixels = data[:w * h]
    if len(pixels) < w * h:
        return None
    return has_header, w, h, bytes(pixels)


def write_bdt_with_header(w: int, h: int, pixels: bytes) -> bytes:
    hdr = struct.pack(BDT_HDR_FMT, BDT_SIG + b'\x00', 1, w, h)
    return hdr + pixels


# ── Control file ──────────────────────────────────────────────────────────────

def parse_ctrl_bitmaps(ctrl_text: str) -> dict:
    """
    Parse a ctrl_*.dat text and return {filename: (index, width, height)}.
    width/height are -1 for entries without explicit dimensions.
    """
    result = {}
    in_bitmaps = False
    for line in ctrl_text.splitlines():
        stripped = line.strip()
        if stripped == '#bitmaps':
            in_bitmaps = True
            continue
        if stripped.startswith('#') and in_bitmaps:
            break   # next section
        if not in_bitmaps or not stripped:
            continue
        parts = stripped.split()
        if not parts or not parts[0].isdigit():
            continue
        idx = int(parts[0])
        fname = parts[-1]
        if not fname.endswith('.bdt'):
            continue
        # Detect optional NxM dimension token
        dim_match = re.match(r'^(\d+)x(\d+)$', parts[1]) if len(parts) >= 3 else None
        if dim_match:
            result[fname] = (idx, int(dim_match.group(1)), int(dim_match.group(2)))
        else:
            result[fname] = (idx, -1, -1)
    return result


def update_ctrl_dimensions(ctrl_text: str, dim_updates: dict) -> str:
    """
    Replace bitmap dimension tokens in ctrl_text.
    dim_updates: {filename: (new_w, new_h)}
    """
    lines = ctrl_text.splitlines(keepends=True)
    out = []
    for line in lines:
        stripped = line.strip()
        parts = stripped.split()
        replaced = False
        if parts and parts[0].isdigit() and parts[-1].endswith('.bdt'):
            fname = parts[-1]
            if fname in dim_updates and len(parts) >= 3:
                dim_match = re.match(r'^(\d+)x(\d+)$', parts[1]) if len(parts) >= 3 else None
                if dim_match:
                    nw, nh = dim_updates[fname]
                    old_dim = parts[1]
                    new_dim = f'{nw}x{nh}'
                    line = line.replace(old_dim, new_dim, 1)
                    replaced = True
        out.append(line)
    return ''.join(out)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='Upscale OpenParsec BDT sprites into gamedata/')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--scale', type=int, default=4, help='Upscale factor (default: 4)')
    parser.add_argument('--assets-dir', default='openparsec-assets')
    args = parser.parse_args()

    assets = args.assets_dir
    outdir = os.path.join(assets, 'gamedata')

    # Load main package
    pack0 = os.path.join(assets, 'pscdata0.dat')
    if not os.path.exists(pack0):
        print(f"Cannot find {pack0}")
        sys.exit(1)

    print(f"Reading {pack0}...")
    pkg = read_package(pack0)

    # Load palette
    pal_bytes = pkg.get('game.pal')
    if not pal_bytes:
        print("ERROR: game.pal not found in package")
        sys.exit(1)
    palette = load_palette(pal_bytes)
    print(f"Loaded 256-colour palette from game.pal")

    # Parse control files to get BDT dimensions
    ctrl_names = [k for k in pkg if re.match(r'^ctrl_.*\.dat$', k)]
    print(f"Found control files: {ctrl_names}")

    # Use ctrl_h_h.dat as the dimension source
    ctrl_text = pkg.get('ctrl_h_h.dat', b'').decode('latin-1')
    ctrl_bitmaps = parse_ctrl_bitmaps(ctrl_text)
    print(f"Found {len(ctrl_bitmaps)} bitmap entries in control file")

    if args.dry_run:
        print("\nDRY RUN — no files will be written\n")
    else:
        os.makedirs(outdir, exist_ok=True)

    # Process BDT files
    total = skipped = 0
    dim_updates = {}   # {bdt_filename: (new_w, new_h)} for ctrl file update

    bdt_files = [(k, v) for k, v in pkg.items() if k.lower().endswith('.bdt')]
    print(f"\nProcessing {len(bdt_files)} BDT files:\n")

    for fname, data in sorted(bdt_files):
        ctrl_entry = ctrl_bitmaps.get(fname, (0, -1, -1))
        ctrl_w, ctrl_h = ctrl_entry[1], ctrl_entry[2]

        result = read_bdt_raw(data, ctrl_w, ctrl_h)
        if result is None:
            print(f"  SKIP  {fname}  (unknown dimensions)")
            skipped += 1
            continue

        has_header, w, h, raw_pixels = result

        # Cap scale
        scale = args.scale
        while scale > 1 and (w * scale > MAX_TEX_SIZE or h * scale > MAX_TEX_SIZE):
            scale //= 2
        if scale <= 1:
            print(f"  SKIP  {fname}  ({w}×{h} already near limit)")
            skipped += 1
            continue

        nw, nh = w * scale, h * scale
        cap_note = f"  [capped to {scale}×]" if scale != args.scale else ""
        print(f"  {w:4}×{h:<4} → {nw:4}×{nh:<4}  {fname}{cap_note}")

        if not args.dry_run:
            # Convert 8-bit → RGBA, upscale, re-quantise
            rgba = indices_to_rgba(raw_pixels, palette)
            im = Image.frombytes('RGBA', (w, h), rgba)
            upscaled = im.resize((nw, nh), Image.LANCZOS)
            new_rgba = upscaled.tobytes()
            new_pixels = rgba_to_indices(new_rgba, palette, nw, nh)

            # Write BDT to gamedata/
            if has_header:
                out_data = write_bdt_with_header(nw, nh, new_pixels)
            else:
                out_data = new_pixels   # raw, dimensions tracked in ctrl file
                dim_updates[fname] = (nw, nh)

            out_path = os.path.join(outdir, fname)
            with open(out_path, 'wb') as f:
                f.write(out_data)
        else:
            if not has_header:
                dim_updates[fname] = (nw, nh)

        total += 1

    # Update and write control files to gamedata/
    if dim_updates:
        print(f"\nUpdating {len(ctrl_names)} control file(s) with new dimensions...")
        for ctrl_name in ctrl_names:
            ctrl_raw = pkg.get(ctrl_name, b'')
            ctrl_str = ctrl_raw.decode('latin-1')
            updated = update_ctrl_dimensions(ctrl_str, dim_updates)
            if not args.dry_run:
                out_path = os.path.join(outdir, ctrl_name)
                with open(out_path, 'wb') as f:
                    f.write(updated.encode('latin-1'))
            print(f"  {'(dry) ' if args.dry_run else ''}→ {ctrl_name}")

    print(f"\nDone. Upscaled: {total}  Skipped: {skipped}")
    if not args.dry_run and total:
        print(f"Output: {outdir}/")
        print(f"Copy the gamedata/ folder alongside the pscdata*.dat files when running the game.")


if __name__ == '__main__':
    main()
