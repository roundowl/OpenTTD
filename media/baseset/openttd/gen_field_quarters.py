#!/usr/bin/env python3
# This file is part of OpenTTD.
# OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
# OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.

"""
Generate the quarter-tile farm field sprites (field_quarters.png / field_quarters.nfo).

Each vanilla farmland ground sprite (one per growth state and slope) is cut into the four
quarters of the tile: a quarter is the half-tile by half-tile square at one tile corner,
projected onto the sloped ground. The four quarter sprites of a tile share the offsets of
the source sprite, so drawing all four at the tile position rebuilds the full tile.

Input is a base graphics set decoded with grfcodec, e.g.:
    grfcodec -d ogfx1_base.grf -o <dir>       (OpenGFX, DOS palette)
Usage:
    gen_field_quarters.py <decoded dir>/ogfx1_base.nfo <output dir>

Order of the output sprites: stage-major, then slope (SlopeToSpriteOffset 0..14),
then quarter ((y half << 1) | x half).
"""

import sys
from pathlib import Path

from PIL import Image

TILE_SIZE = 16
TILE_HEIGHT = 8
SLOPE_COUNT = 15  # flat plus the 14 non-steep slopes

# Source sprite of each FieldStage: Fallow, Cultivated, Sown, Sprouted, Growing, Maturing, Ripe, Overripe, Withered.
SPR_FARMLAND_BARE = 4126
SPR_FARMLAND_STATE = [4145, 4164, 4183, 4202, 4221, 4240, 4259]  # STATE_1 .. STATE_7
SPR_FARMLAND_HAYPACKS = 4278
STAGE_SOURCES = [
    SPR_FARMLAND_HAYPACKS,   # Fallow: stubble after harvest
    SPR_FARMLAND_BARE,       # Cultivated
    SPR_FARMLAND_STATE[0],   # Sown
    SPR_FARMLAND_STATE[1],   # Sprouted
    SPR_FARMLAND_STATE[2],   # Growing
    SPR_FARMLAND_STATE[3],   # Maturing
    SPR_FARMLAND_STATE[4],   # Ripe
    SPR_FARMLAND_STATE[5],   # Overripe
    SPR_FARMLAND_STATE[6],   # Withered
]

SLOPE_W, SLOPE_S, SLOPE_E, SLOPE_N = 1, 2, 4, 8


def partial_z(x: float, y: float, slope: int) -> float:
    """Height of the ground at (x, y) within a tile; continuous version of GetPartialPixelZ."""
    ts, th = TILE_SIZE, TILE_HEIGHT
    if slope == 0:
        return 0
    if slope == SLOPE_N:
        return (ts - x - y) / 2 if x + y <= ts else 0
    if slope == SLOPE_E:
        return (y - x) / 2 if y >= x else 0
    if slope == SLOPE_S:
        return (x + y - ts) / 2 if x + y >= ts else 0
    if slope == SLOPE_W:
        return (x - y) / 2 if x >= y else 0
    if slope == SLOPE_N | SLOPE_E:
        return (ts - x) / 2
    if slope == SLOPE_S | SLOPE_E:
        return y / 2
    if slope == SLOPE_S | SLOPE_W:
        return x / 2
    if slope == SLOPE_N | SLOPE_W:
        return (ts - y) / 2
    if slope == SLOPE_E | SLOPE_N | SLOPE_W:
        return th - (x + y - ts) / 2 if x + y >= ts else th
    if slope == SLOPE_S | SLOPE_E | SLOPE_N:
        return th - (x - y) / 2 if y < x else th
    if slope == SLOPE_W | SLOPE_S | SLOPE_E:
        return th - (ts - x - y) / 2 if x + y <= ts else th
    if slope == SLOPE_N | SLOPE_W | SLOPE_S:
        return th - (y - x) / 2 if x < y else th
    if slope == SLOPE_N | SLOPE_S:
        return (ts - x - y) / 2 if x + y < ts else (x + y - ts) / 2
    if slope == SLOPE_E | SLOPE_W:
        return (x - y) / 2 if x >= y else (y - x) / 2
    raise ValueError(f"unsupported slope {slope}")


def project(x: float, y: float, z: float) -> tuple[float, float]:
    """World offset within a tile to screen offset from the tile's north corner (RemapCoords)."""
    return 2 * (y - x), x + y - z


def quarter_outline(quarter: int, slope: int) -> list[tuple[float, float]]:
    """Screen outline of a quarter: tile corner, edge midpoint, tile centre, edge midpoint."""
    h = TILE_SIZE / 2
    x0 = (quarter & 1) * h
    y0 = (quarter >> 1) * h
    corners = [(x0, y0), (x0 + h, y0), (x0 + h, y0 + h), (x0, y0 + h)]
    return [project(x, y, partial_z(x, y, slope)) for x, y in corners]


def inside(px: float, py: float, poly: list[tuple[float, float]]) -> bool:
    """Even-odd point in polygon test."""
    result = False
    n = len(poly)
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        if (y1 > py) != (y2 > py):
            if px < x1 + (py - y1) * (x2 - x1) / (y2 - y1):
                result = not result
    return result


def centroid(poly: list[tuple[float, float]]) -> tuple[float, float]:
    return sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly)


def classify(img: Image.Image, xrel: int, yrel: int, slope: int) -> list[list[int]]:
    """Assign every opaque pixel of a full-tile sprite to a quarter (-1 for transparent)."""
    outlines = [quarter_outline(q, slope) for q in range(4)]
    centres = [centroid(o) for o in outlines]
    w, h = img.size
    pix = img.load()
    labels = [[-1] * w for _ in range(h)]
    for py in range(h):
        for px in range(w):
            if pix[px, py] == 0:
                continue
            sx, sy = px + xrel + 0.5, py + yrel + 0.5
            for q in range(4):
                if inside(sx, sy, outlines[q]):
                    labels[py][px] = q
                    break
            else:
                # Edge pixel just outside the tile outline: give it to the nearest quarter.
                labels[py][px] = min(range(4), key=lambda q: (centres[q][0] - sx) ** 2 + (centres[q][1] - sy) ** 2)
    return labels


def parse_nfo(nfo: Path) -> dict[int, tuple[Path, int, int, int, int, int, int]]:
    """Map sprite number to (png, x, y, w, h, xrel, yrel) for normal-zoom 8bpp sprites."""
    sprites = {}
    for line in nfo.read_text(encoding="latin-1").splitlines():
        tokens = line.split()
        if len(tokens) < 10 or tokens[0].startswith("//") or not tokens[0].lstrip("|").isdigit():
            continue
        if tokens[2] != "8bpp" or tokens[9] != "normal":
            continue
        num = int(tokens[0].lstrip("|"))
        x, y, w, h, xrel, yrel = (int(t) for t in tokens[3:9])
        png = nfo.parent / tokens[1]
        if not png.exists():
            png = nfo.parent.parent / tokens[1]
        sprites.setdefault(num, (png, x, y, w, h, xrel, yrel))
    return sprites


def main() -> None:
    nfo_in, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    sprites = parse_nfo(nfo_in)
    sheets: dict[Path, Image.Image] = {}

    cell_w, cell_h = 72, 64
    per_row = SLOPE_COUNT * 4
    rows = len(STAGE_SOURCES)
    out = None
    nfo_lines = []

    for stage, base in enumerate(STAGE_SOURCES):
        for slope in range(SLOPE_COUNT):
            png, x, y, w, h, xrel, yrel = sprites[base + slope]
            if png not in sheets:
                sheets[png] = Image.open(png)
            sheet = sheets[png]
            if out is None:
                out = Image.new("P", (per_row * cell_w, rows * cell_h), 0)
                out.putpalette(sheet.getpalette())
            src = sheet.crop((x, y, x + w, y + h))
            labels = classify(src, xrel, yrel, slope)
            src_pix = src.load()
            for q in range(4):
                part = Image.new("P", (w, h), 0)
                part_pix = part.load()
                for py in range(h):
                    for px in range(w):
                        if labels[py][px] == q:
                            part_pix[px, py] = src_pix[px, py]
                bbox = part.getbbox() or (0, 0, 1, 1)
                part = part.crop(bbox)
                cx = (slope * 4 + q) * cell_w
                cy = stage * cell_h
                out.paste(part, (cx, cy))
                nfo_lines.append(f"   -1 sprites/field_quarters.png      8bpp {cx:5d} {cy:4d} {part.width:4d} {part.height:4d} {xrel + bbox[0]:4d} {yrel + bbox[1]:4d} normal")

    out_dir.mkdir(parents=True, exist_ok=True)
    out.save(out_dir / "field_quarters.png", optimize=True)
    header = [
        "// Farm fork: quarter-tile farm field ground. Generated by gen_field_quarters.py from OpenGFX farmland sprites.",
        "// Order: stage (9), then slope (15, SlopeToSpriteOffset), then quarter ((y half << 1) | x half).",
        '   -1 * 0\t 0C "Farm field quarters"',
        f"   -1 * 0\t 05 40 FF \\w{len(nfo_lines)}",
    ]
    (out_dir / "field_quarters.nfo").write_text("\n".join(header + nfo_lines) + "\n")
    print(f"{len(nfo_lines)} sprites written to {out_dir}")


if __name__ == "__main__":
    main()
