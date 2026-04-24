#!/usr/bin/env python3
"""
make_planet_tex.py — Procedural planetary texture generator for OpenParsec
Generates 512x256 uncompressed TGA files with no external dependencies.

Usage:
    python3 make_planet_tex.py [output_dir]

Output files (drop into openparsec-assets/textures/ or wherever textures live):
    planet_terra.tga   — blue ocean, green/brown continents, white polar caps
    planet_mars.tga    — red/orange rocky desert with dark basalt patches
    planet_gas.tga     — Jupiter-style horizontal colour bands
    planet_ice.tga     — pale blue/white frozen world
    planet_lava.tga    — dark crust with glowing orange magma cracks
    planet_ocean.tga   — deep blue with swirling storm patterns
"""

import math
import os
import random
import struct
import sys

# ---------------------------------------------------------------------------
# TGA writer (type 2: uncompressed true-colour)
# ---------------------------------------------------------------------------

def write_tga(path: str, width: int, height: int, pixels: list) -> None:
    """Write an uncompressed 24-bit TGA file.
    pixels: flat list of (r, g, b) tuples, row 0 = bottom (TGA convention).
    """
    with open(path, "wb") as f:
        # Header
        f.write(struct.pack(
            "<BBBHHBHHHHBB",
            0,      # id length
            0,      # colour map type
            2,      # image type: uncompressed RGB
            0, 0,   # colour map origin, length
            0,      # colour map depth
            0, 0,   # x origin, y origin
            width, height,
            24,     # bits per pixel
            0x00,   # image descriptor (top-left origin bit = 0 → bottom-left)
        ))
        # Pixel data in BGR order
        for r, g, b in pixels:
            f.write(struct.pack("BBB", b, g, r))


# ---------------------------------------------------------------------------
# Noise primitives (pure Python, no numpy)
# ---------------------------------------------------------------------------

def _hash(x: int, y: int, seed: int = 0) -> float:
    """Deterministic pseudo-random float in [0, 1) from integer grid coords."""
    h = (x * 1619 + y * 31337 + seed * 6971) & 0xFFFFFFFF
    h = (h ^ (h >> 16)) * 0x45D9F3B & 0xFFFFFFFF
    h = (h ^ (h >> 16)) * 0x45D9F3B & 0xFFFFFFFF
    h = (h ^ (h >> 16)) & 0xFFFFFFFF
    return h / 0xFFFFFFFF


def _smooth(t: float) -> float:
    return t * t * t * (t * (t * 6 - 15) + 10)


def _lerp(a: float, b: float, t: float) -> float:
    return a + t * (b - a)


def value_noise(x: float, y: float, seed: int = 0) -> float:
    """Smooth value noise in [0, 1) — NOT seamless, use value_noise_tiled for wrapping."""
    xi, yi = int(math.floor(x)), int(math.floor(y))
    xf, yf = x - xi, y - yi
    u, v = _smooth(xf), _smooth(yf)
    a = _lerp(_hash(xi, yi, seed),     _hash(xi + 1, yi,     seed), u)
    b = _lerp(_hash(xi, yi + 1, seed), _hash(xi + 1, yi + 1, seed), u)
    return _lerp(a, b, v)


def value_noise_tiled(x: float, y: float, seed: int, tile_x: int) -> float:
    """Value noise that tiles seamlessly in the x direction.
    tile_x: number of grid cells per tile period (must be a positive integer).
    The noise at x=0 exactly equals the noise at x=tile_x.
    """
    xi, yi = int(math.floor(x)), int(math.floor(y))
    xf, yf = x - xi, y - yi
    u, v = _smooth(xf), _smooth(yf)
    xi0 = xi % tile_x
    xi1 = (xi + 1) % tile_x
    a = _lerp(_hash(xi0, yi,     seed), _hash(xi1, yi,     seed), u)
    b = _lerp(_hash(xi0, yi + 1, seed), _hash(xi1, yi + 1, seed), u)
    return _lerp(a, b, v)


def fbm(x: float, y: float, octaves: int = 6, seed: int = 0,
        lacunarity: float = 2.0, gain: float = 0.5) -> float:
    """Fractional Brownian motion — returns [0, 1). Not seamless."""
    value = 0.0
    amplitude = 0.5
    frequency = 1.0
    max_val = 0.0
    for _ in range(octaves):
        value    += amplitude * value_noise(x * frequency, y * frequency, seed)
        max_val  += amplitude
        amplitude *= gain
        frequency *= lacunarity
    return value / max_val


def fbm_tiled(x: float, y: float, octaves: int, seed: int, base_tile_x: float,
              lacunarity: float = 2.0, gain: float = 0.5) -> float:
    """FBM that tiles seamlessly in x.
    base_tile_x: the tile period at the base frequency (before lacunarity scaling).
    At each octave the tile period scales by lacunarity so every octave also tiles.
    Works perfectly when base_tile_x * lacunarity^i is always an integer.
    """
    value = 0.0
    amplitude = 0.5
    frequency = 1.0
    max_val = 0.0
    tile = base_tile_x
    for _ in range(octaves):
        tile_i = max(1, round(tile))
        value    += amplitude * value_noise_tiled(x * frequency, y * frequency, seed, tile_i)
        max_val  += amplitude
        amplitude *= gain
        frequency *= lacunarity
        tile      *= lacunarity
    return value / max_val


def turbulence(x: float, y: float, octaves: int = 6, seed: int = 0) -> float:
    """Absolute-value FBM for cloud/turbulence patterns. Not seamless."""
    value = 0.0
    amplitude = 0.5
    frequency = 1.0
    max_val = 0.0
    for _ in range(octaves):
        value    += amplitude * abs(value_noise(x * frequency, y * frequency, seed) * 2 - 1)
        max_val  += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    return value / max_val


def turbulence_tiled(x: float, y: float, octaves: int, seed: int,
                     base_tile_x: float) -> float:
    """Absolute-value FBM that tiles seamlessly in x."""
    value = 0.0
    amplitude = 0.5
    frequency = 1.0
    max_val = 0.0
    tile = base_tile_x
    for _ in range(octaves):
        tile_i = max(1, round(tile))
        n = value_noise_tiled(x * frequency, y * frequency, seed, tile_i) * 2 - 1
        value    += amplitude * abs(n)
        max_val  += amplitude
        amplitude *= 0.5
        frequency *= 2.0
        tile      *= 2.0
    return value / max_val


def clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, v))


def lerp_colour(a, b, t: float):
    t = clamp(t)
    return (
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def mix_colour(colours, stops, t: float):
    """Multi-stop gradient. colours and stops are parallel lists."""
    t = clamp(t)
    for i in range(len(stops) - 1):
        if t <= stops[i + 1]:
            local = (t - stops[i]) / (stops[i + 1] - stops[i])
            return lerp_colour(colours[i], colours[i + 1], local)
    return colours[-1]


# ---------------------------------------------------------------------------
# Texture generators
# All noise sampled with tiled variants so left == right edge (seamless wrap).
# The v (latitude) axis is NOT tiled — poles are different.
# ---------------------------------------------------------------------------

WIDTH, HEIGHT = 512, 256


def gen_terra(seed: int = 1) -> list:
    """Blue ocean, green/brown continents, white polar caps."""
    deep_ocean  = (  8,  60, 120)
    shallow_sea = ( 30, 100, 170)
    sand        = (200, 180, 110)
    lowland     = ( 60, 120,  40)
    highland    = ( 80, 100,  50)
    mountain    = (140, 130, 110)
    snow        = (240, 245, 255)
    polar       = (220, 235, 255)

    pixels = []
    for row in range(HEIGHT):
        lat = (row / HEIGHT) * math.pi        # 0 (bottom/south) → π (top/north)
        lat_norm = row / HEIGHT               # 0 → 1

        for col in range(WIDTH):
            u = col / WIDTH
            v = lat_norm

            # continent noise — tiled at base scale 3, detail at 8, polar at 10
            land   = fbm_tiled(u * 3, v * 3, octaves=7, seed=seed,       base_tile_x=3)
            detail = fbm_tiled(u * 8, v * 8, octaves=4, seed=seed + 1,   base_tile_x=8) * 0.25

            h = land + detail - 0.05

            # polar caps fade based on latitude
            pole_mask = max(0.0, (abs(lat_norm - 0.5) * 2) ** 2.5 - 0.55) * 2.0

            if pole_mask > 0.6:
                c = lerp_colour(snow, polar,
                                value_noise_tiled(u * 10, v * 10, seed + 5, 10))
            elif h < 0.42:
                depth = (0.42 - h) / 0.42
                c = lerp_colour(shallow_sea, deep_ocean, depth)
            elif h < 0.46:
                c = lerp_colour(sand, shallow_sea, (0.46 - h) / 0.04)
            elif h < 0.55:
                c = lerp_colour(lowland, sand, (0.55 - h) / 0.09)
            elif h < 0.65:
                c = lerp_colour(highland, lowland, (0.65 - h) / 0.10)
            elif h < 0.75:
                c = lerp_colour(mountain, highland, (0.75 - h) / 0.10)
            else:
                c = lerp_colour(snow, mountain, (h - 0.75) / 0.25)

            # blend polar
            if pole_mask > 0.0 and pole_mask <= 0.6:
                c = lerp_colour(c, polar, pole_mask)

            pixels.append(c)

    return pixels


def gen_mars(seed: int = 2) -> list:
    """Red/orange rocky desert with dark basalt patches."""
    rust     = (180,  60,  20)
    orange   = (200,  90,  35)
    pale     = (210, 140,  90)
    basalt   = ( 90,  45,  30)
    dust     = (195, 130,  70)

    pixels = []
    for row in range(HEIGHT):
        lat_norm = row / HEIGHT
        for col in range(WIDTH):
            u = col / WIDTH
            n  = fbm_tiled(u * 4, lat_norm * 4, octaves=7, seed=seed,     base_tile_x=4)
            t  = turbulence_tiled(u * 6, lat_norm * 6, octaves=5, seed=seed + 2, base_tile_x=6)

            h = n * 0.7 + t * 0.3

            stops   = [0.0, 0.25, 0.50, 0.70, 0.85, 1.0]
            colours = [basalt, rust, orange, pale, dust, pale]
            c = mix_colour(colours, stops, h)

            # slight polar frost on top and bottom
            pole_dist = abs(lat_norm - 0.5) * 2
            if pole_dist > 0.92:
                frost = (210, 200, 190)
                c = lerp_colour(c, frost, (pole_dist - 0.92) / 0.08 * 0.7)

            pixels.append(c)

    return pixels


def gen_gas(seed: int = 3) -> list:
    """Jupiter-style horizontal colour bands with turbulent edges."""
    band_colours = [
        (200, 150,  90),   # tan
        (160, 100,  50),   # brown
        (230, 190, 130),   # pale cream
        (180,  80,  40),   # dark rust
        (220, 170,  90),   # amber
        (240, 210, 160),   # cream
        (150,  90,  50),   # deep brown
        (200, 130,  60),   # orange-tan
        (230, 200, 150),   # light cream
        (170,  70,  30),   # dark rust
    ]

    pixels = []
    for row in range(HEIGHT):
        lat_norm = row / HEIGHT
        for col in range(WIDTH):
            u = col / WIDTH

            # warp latitude with turbulence (tiled so u=0 matches u=1)
            warp = turbulence_tiled(u * 2, lat_norm * 2, octaves=5, seed=seed, base_tile_x=2) * 0.12
            warped_lat = clamp(lat_norm + warp - 0.06)

            # band selection
            band_t = warped_lat * len(band_colours)
            band_i = int(band_t) % len(band_colours)
            band_j = (band_i + 1) % len(band_colours)
            band_frac = band_t - int(band_t)

            c = lerp_colour(band_colours[band_i], band_colours[band_j], band_frac)

            # overlay cloud wisps (tiled)
            cloud = fbm_tiled(u * 5, lat_norm * 3, octaves=4, seed=seed + 10, base_tile_x=5) ** 2
            if cloud > 0.6:
                c = lerp_colour(c, (240, 225, 200), (cloud - 0.6) / 0.4 * 0.5)

            pixels.append(c)

    return pixels


def gen_ice(seed: int = 4) -> list:
    """Pale blue/white frozen world with icy cracks."""
    glacier   = (200, 225, 245)
    ice_deep  = (130, 180, 220)
    blue_ice  = ( 80, 140, 200)
    crack     = ( 60, 100, 160)
    snow_cap  = (240, 248, 255)

    pixels = []
    for row in range(HEIGHT):
        lat_norm = row / HEIGHT
        for col in range(WIDTH):
            u = col / WIDTH

            base   = fbm_tiled(u * 4,  lat_norm * 4,  octaves=6, seed=seed,     base_tile_x=4)
            cracks = turbulence_tiled(u * 12, lat_norm * 12, octaves=4, seed=seed + 1, base_tile_x=12)

            # crack lines: low turbulence = crack
            crack_mask = clamp(1.0 - cracks * 3.0)

            c = mix_colour(
                [crack, blue_ice, ice_deep, glacier, snow_cap],
                [0.0, 0.15, 0.40, 0.65, 1.0],
                base,
            )

            if crack_mask > 0.1:
                c = lerp_colour(c, crack, crack_mask * 0.5)

            # brighter poles
            pole_dist = abs(lat_norm - 0.5) * 2
            if pole_dist > 0.7:
                c = lerp_colour(c, snow_cap, (pole_dist - 0.7) / 0.3)

            pixels.append(c)

    return pixels


def gen_lava(seed: int = 5) -> list:
    """Dark volcanic crust with glowing orange/red magma cracks."""
    obsidian  = ( 15,  10,  10)
    dark_rock = ( 35,  25,  20)
    rock      = ( 60,  45,  35)
    hot_rock  = ( 90,  40,  10)
    magma     = (220,  80,  10)
    glow      = (255, 160,   0)
    bright    = (255, 220,  80)

    pixels = []
    for row in range(HEIGHT):
        lat_norm = row / HEIGHT
        for col in range(WIDTH):
            u = col / WIDTH

            base  = fbm_tiled(u * 3, lat_norm * 3, octaves=6, seed=seed,     base_tile_x=3)
            crack = turbulence_tiled(u * 8, lat_norm * 8, octaves=5, seed=seed + 3, base_tile_x=8)

            # cracks glow brightest where turbulence is low
            heat = clamp(1.0 - crack * 2.5) ** 2

            c = mix_colour(
                [obsidian, dark_rock, rock],
                [0.0, 0.4, 1.0],
                base,
            )

            if heat > 0.05:
                glow_c = mix_colour(
                    [hot_rock, magma, glow, bright],
                    [0.0, 0.3, 0.6, 1.0],
                    heat,
                )
                c = lerp_colour(c, glow_c, clamp(heat * 1.5))

            pixels.append(c)

    return pixels


def gen_ocean(seed: int = 6) -> list:
    """Deep ocean planet with swirling storm vortices."""
    abyss   = (  5,  20,  60)
    deep    = ( 15,  50, 120)
    mid     = ( 30,  90, 160)
    shallow = ( 60, 130, 190)
    foam    = (180, 210, 235)
    storm   = ( 20,  60, 110)

    pixels = []
    for row in range(HEIGHT):
        lat_norm = row / HEIGHT
        for col in range(WIDTH):
            u = col / WIDTH

            # Warp UV — use tiled fbm so warp_u(u=0) == warp_u(u=1).
            # Use modulo (not clamp) so the wrapping is preserved in wu.
            warp_u = fbm_tiled(u * 3, lat_norm * 3, octaves=4, seed=seed + 10, base_tile_x=3) * 0.15
            warp_v = fbm_tiled(u * 3, lat_norm * 3, octaves=4, seed=seed + 11, base_tile_x=3) * 0.15

            wu = (u + warp_u - 0.075) % 1.0          # modulo preserves seamlessness
            wv = clamp(lat_norm + warp_v - 0.075)     # vertical: clamp is fine (no v-tiling)

            depth = fbm_tiled(wu * 4,  wv * 4,  octaves=7, seed=seed,     base_tile_x=4)
            surf  = fbm_tiled(wu * 10, wv * 6,  octaves=4, seed=seed + 2, base_tile_x=10)

            c = mix_colour(
                [abyss, deep, mid, shallow],
                [0.0, 0.35, 0.65, 1.0],
                depth,
            )

            # foam on high surf
            if surf > 0.72:
                c = lerp_colour(c, foam, (surf - 0.72) / 0.28 * 0.8)

            # storm swirls — darker bands
            storm_n = turbulence_tiled(wu * 5, wv * 2, octaves=5, seed=seed + 4, base_tile_x=5)
            if storm_n < 0.35:
                c = lerp_colour(c, storm, (0.35 - storm_n) / 0.35 * 0.4)

            pixels.append(c)

    return pixels


# ---------------------------------------------------------------------------
# Ring texture generators  (64 wide × 64 tall — matches PLANET_RING_TEX_HEIGHT)
#
# V axis: row 0 = ring inner edge (closest to planet),
#         row 63 = ring outer edge (farthest from planet)
# U axis: wraps around the circumference (256 segments → 64 texels repeating)
# ---------------------------------------------------------------------------

RWIDTH, RHEIGHT = 64, 64


def _ring_band(v_norm: float, bands: list) -> float:
    """Return 0..1 density for a set of (centre_v, width, peak) band specs."""
    total = 0.0
    for (cen, wid, peak) in bands:
        dist = abs(v_norm - cen)
        if dist < wid:
            total += peak * (1.0 - dist / wid) ** 2
    return clamp(total)


def gen_ring_saturn(seed: int = 10) -> list:
    """Saturn-like rings: warm tan/beige bands with dark Cassini-style gaps."""
    bands = [
        (0.08, 0.07, 1.0),   # inner dense band
        (0.22, 0.08, 0.85),  # B ring
        (0.32, 0.02, 0.10),  # Cassini division (gap)
        (0.42, 0.09, 0.70),  # A ring
        (0.58, 0.05, 0.45),  # outer band
        (0.72, 0.06, 0.25),  # faint outer ring
        (0.88, 0.08, 0.12),  # diffuse edge
    ]
    tan   = (210, 185, 130)
    beige = (235, 215, 165)
    dark  = ( 30,  25,  18)

    pixels = []
    for row in range(RHEIGHT):
        v = row / RHEIGHT
        for col in range(RWIDTH):
            u = col / RWIDTH
            density = _ring_band(v, bands)
            # tiled noise variation around circumference
            noise = value_noise_tiled(u * 16, v * 4, seed, 16) * 0.15
            density = clamp(density + noise - 0.07)
            shimmer = value_noise_tiled(u * 8, v * 8, seed + 1, 8)
            c = lerp_colour(dark, lerp_colour(tan, beige, shimmer), density)
            pixels.append(c)
    return pixels


def gen_ring_ice(seed: int = 11) -> list:
    """Bright icy rings — blue-white uniform bands, like Uranus."""
    bands = [
        (0.12, 0.08, 1.0),
        (0.30, 0.05, 0.80),
        (0.45, 0.10, 0.90),
        (0.65, 0.06, 0.60),
        (0.82, 0.07, 0.35),
    ]
    ice_white = (230, 245, 255)
    ice_blue  = (140, 195, 230)
    gap       = ( 20,  30,  45)

    pixels = []
    for row in range(RHEIGHT):
        v = row / RHEIGHT
        for col in range(RWIDTH):
            u = col / RWIDTH
            density = _ring_band(v, bands)
            shimmer = value_noise_tiled(u * 20, v * 6, seed, 20) * 0.12
            density = clamp(density + shimmer - 0.05)
            c = lerp_colour(gap, lerp_colour(ice_blue, ice_white, density), density)
            pixels.append(c)
    return pixels


def gen_ring_dust(seed: int = 12) -> list:
    """Faint reddish-brown dust ring — single diffuse band, like Mars's Phobos ring."""
    bands = [
        (0.50, 0.45, 0.55),   # single wide diffuse band
    ]
    dust  = (160,  90,  50)
    dark  = ( 15,  10,   8)

    pixels = []
    for row in range(RHEIGHT):
        v = row / RHEIGHT
        for col in range(RWIDTH):
            u = col / RWIDTH
            density = _ring_band(v, bands)
            wisp = fbm_tiled(u * 12, v * 6, octaves=4, seed=seed, base_tile_x=12) * 0.3
            density = clamp(density * 0.7 + wisp)
            c = lerp_colour(dark, dust, density)
            pixels.append(c)
    return pixels


def gen_ring_dense(seed: int = 13) -> list:
    """Thick opaque ring, deep orange — like a young protoplanetary disc."""
    bands = [
        (0.10, 0.09, 1.00),
        (0.28, 0.12, 0.95),
        (0.48, 0.10, 0.85),
        (0.65, 0.12, 0.75),
        (0.83, 0.10, 0.55),
    ]
    inner = (200,  80,  20)
    outer = (140,  55,  15)
    dark  = ( 25,  15,   8)

    pixels = []
    for row in range(RHEIGHT):
        v = row / RHEIGHT
        for col in range(RWIDTH):
            u = col / RWIDTH
            density = _ring_band(v, bands)
            n = fbm_tiled(u * 10, v * 5, octaves=5, seed=seed, base_tile_x=10) * 0.2
            density = clamp(density + n - 0.08)
            base_col = lerp_colour(inner, outer, v)
            c = lerp_colour(dark, base_col, density)
            pixels.append(c)
    return pixels


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

TEXTURES = [
    ("planet_terra.tga", gen_terra,  1, WIDTH, HEIGHT),
    ("planet_mars.tga",  gen_mars,   2, WIDTH, HEIGHT),
    ("planet_gas.tga",   gen_gas,    3, WIDTH, HEIGHT),
    ("planet_ice.tga",   gen_ice,    4, WIDTH, HEIGHT),
    ("planet_lava.tga",  gen_lava,   5, WIDTH, HEIGHT),
    ("planet_ocean.tga", gen_ocean,  6, WIDTH, HEIGHT),
    # ring textures — 64×64
    ("ring_saturn.tga",  gen_ring_saturn, 10, RWIDTH, RHEIGHT),
    ("ring_ice.tga",     gen_ring_ice,    11, RWIDTH, RHEIGHT),
    ("ring_dust.tga",    gen_ring_dust,   12, RWIDTH, RHEIGHT),
    ("ring_dense.tga",   gen_ring_dense,  13, RWIDTH, RHEIGHT),
]


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out_dir, exist_ok=True)

    for filename, gen_fn, seed, w, h in TEXTURES:
        path = os.path.join(out_dir, filename)
        print(f"Generating {filename} ({w}x{h})...", end=" ", flush=True)
        pixels = gen_fn(seed)
        # TGA stores rows bottom-to-top; we generate top-to-bottom, so flip
        rows = [pixels[r * w:(r + 1) * w] for r in range(h)]
        rows_flipped = list(reversed(rows))
        flat = [px for row in rows_flipped for px in row]
        write_tga(path, w, h, flat)
        print("done")

    print(f"\nAll textures written to: {os.path.abspath(out_dir)}")
    print("\nPlanet surface:  sv.planet ... tex planet_terra")
    print("Planet with ring: sv.planet ... ring 1 ringtex ring_saturn ringinner 80 ringouter 300")
    print("(omit the .tga extension — the engine appends it)")


if __name__ == "__main__":
    main()
