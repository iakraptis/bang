#!/usr/bin/env python3
"""Pull a voxel palette out of each character's card art.

The voxel figures on the 3D stage should read as the character on the card,
so their colours start from the portrait rather than from invention: the
crown of the hat, the face, the hair beside it, and the coat across the
chest. Output is a JSON map keyed by card key, to seed the CHARACTERS table
in web/voxel-models.js.

The portraits are not framed identically, so treat this as a starting point
and not an oracle — a bare-headed character's hat box reads the parchment
behind them, and a wide brim can shade the hair box. Anything it cannot
answer comes back null rather than as a plausible wrong colour.

    python3 tools/extract_voxel_palette.py web/cards [key ...]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# The portrait sits between the title bar and the rules panel, and the frame
# runs a few percent in from every edge.
PORTRAIT = (0.13, 0.17, 0.87, 0.61)

# Regions within the portrait, as fractions of it: (x0, y0, x1, y1).
REGIONS = {
    "hat": (0.28, 0.00, 0.72, 0.15),
    "hair": (0.24, 0.16, 0.76, 0.30),
    "skin": (0.42, 0.26, 0.58, 0.40),
    "coat": (0.18, 0.74, 0.82, 1.00),
    "trim": (0.40, 0.54, 0.60, 0.72),
}


def dominant(pixels: np.ndarray, paper: np.ndarray) -> tuple[int, int, int]:
    """The most common colour in a patch, quantised so near-identical shades
    of the same cloth count as one.

    The cards are printed on a single parchment tone that fills every gap
    around the figure; left in, it wins nearly every patch. It is measured
    from the card's own margin and masked out here, along with the ink
    outlines and the paper-white highlights.
    """
    if pixels.size == 0:
        return (0, 0, 0)
    flat = pixels.reshape(-1, 3).astype(np.int32)
    luma = flat @ np.array([0.299, 0.587, 0.114])
    off_paper = np.abs(flat - paper).sum(axis=1) > 60
    keep = (luma > 28) & (luma < 236) & off_paper
    # A patch that is nothing but parchment (a bare-headed character's hat
    # box) has no answer; say so rather than inventing one.
    if keep.sum() < max(12, flat.shape[0] // 50):
        return None
    flat = flat[keep]
    quantised = (flat // 24) * 24
    keys = quantised[:, 0] * 65536 + quantised[:, 1] * 256 + quantised[:, 2]
    values, counts = np.unique(keys, return_counts=True)
    winner = values[counts.argmax()]
    bucket = np.array([winner // 65536, (winner // 256) % 256, winner % 256])
    # Average the true pixels inside the winning bucket for a truer shade.
    member = (quantised == bucket).all(axis=1)
    return tuple(int(round(value)) for value in flat[member].mean(axis=0))


def hexed(rgb) -> str | None:
    return None if rgb is None else "#{:02x}{:02x}{:02x}".format(*rgb)


def paper_tone(image: np.ndarray) -> np.ndarray:
    """The parchment the card is printed on, read off the rules panel at the
    foot of the card where nothing is drawn over it."""
    height, width = image.shape[:2]
    strip = image[int(height * 0.90):int(height * 0.95),
                  int(width * 0.20):int(width * 0.80)]
    return np.median(strip.reshape(-1, 3), axis=0).astype(np.int32)


def palette_for(path: Path) -> dict[str, str]:
    image = np.asarray(Image.open(path).convert("RGB"))
    paper = paper_tone(image)
    height, width = image.shape[:2]
    px0, py0, px1, py1 = PORTRAIT
    portrait = image[int(height * py0):int(height * py1),
                     int(width * px0):int(width * px1)]
    ph, pw = portrait.shape[:2]
    palette = {}
    for name, (x0, y0, x1, y1) in REGIONS.items():
        patch = portrait[
            int(ph * y0):max(int(ph * y1), int(ph * y0) + 1),
            int(pw * x0):max(int(pw * x1), int(pw * x0) + 1),
        ]
        palette[name] = hexed(dominant(patch, paper))
    return palette


def main(argv: list[str]) -> int:
    cards = Path(argv[1] if len(argv) > 1 else "web/cards")
    keys = argv[2:] if len(argv) > 2 else None
    result = {}
    for path in sorted(cards.glob("*.png")):
        if keys and path.stem not in keys:
            continue
        result[path.stem] = palette_for(path)
    json.dump(result, sys.stdout, indent=2, sort_keys=True)
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
