#!/usr/bin/env python3
"""Convert toPNGplease/*.svg → assets/studio/icons/*.png (64×64)."""
import sys
from pathlib import Path

try:
    import cairosvg
except ImportError:
    print("Install cairosvg first: pip install cairosvg", file=sys.stderr)
    sys.exit(1)

root = Path(__file__).resolve().parents[1]
src = root / "toPNGplease"
dst = root / "assets" / "studio" / "icons"
dst.mkdir(parents=True, exist_ok=True)

count = 0
for svg in sorted(src.glob("*.svg")):
    out = dst / (svg.stem + ".png")
    cairosvg.svg2png(url=str(svg), write_to=str(out), output_width=64, output_height=64)
    print(f"  {svg.name} → {out.relative_to(root)}")
    count += 1
print(f"Converted {count} icons.")
