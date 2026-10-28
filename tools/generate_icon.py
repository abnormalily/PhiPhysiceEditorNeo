#!/usr/bin/env python3
"""Generate the PhiPhysiceEditorNeo program icon.

The mark is four note-shaped bars forming a diamond around a downward arrow.
Run this script after changing the drawing and commit the generated files:
  python tools/generate_icon.py
"""

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SIZE = 1024
CENTER = SIZE / 2
RADIUS = 362
BAR_WIDTH = 150
OUTLINE = 18
DARK = (8, 13, 27, 255)
COLORS = [
    (125, 179, 247, 255),  # Tap blue
    (248, 160, 204, 255),  # Flick pink
    (140, 232, 194, 255),  # Hold green
    (253, 214, 122, 255),  # Drag yellow
]


def rounded_bar(draw: ImageDraw.ImageDraw, a, b, color):
    """A note-shaped bar with rounded ends and a dark outline."""
    draw.line([a, b], fill=DARK, width=BAR_WIDTH + OUTLINE * 2)
    draw.line([a, b], fill=color, width=BAR_WIDTH)
    for point in (a, b):
        r = BAR_WIDTH / 2 + OUTLINE
        draw.ellipse(
            [point[0] - r, point[1] - r, point[0] + r, point[1] + r], fill=DARK
        )
        r = BAR_WIDTH / 2
        draw.ellipse(
            [point[0] - r, point[1] - r, point[0] + r, point[1] + r], fill=color
        )
def build_icon() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    points = [
        (CENTER, CENTER - RADIUS),
        (CENTER + RADIUS, CENTER),
        (CENTER, CENTER + RADIUS),
        (CENTER - RADIUS, CENTER),
    ]
    gap = 70
    for i in range(4):
        a = points[i]
        b = points[(i + 1) % 4]
        length = ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5
        ux = (b[0] - a[0]) / length
        uy = (b[1] - a[1]) / length
        start = (a[0] + ux * gap, a[1] + uy * gap)
        end = (b[0] - ux * gap, b[1] - uy * gap)
        rounded_bar(draw, start, end, COLORS[i])

    # Downward arrow in the diamond opening.
    arrow = [
        (CENTER - 38, CENTER - 165),
        (CENTER + 38, CENTER - 165),
        (CENTER + 38, CENTER + 58),
        (CENTER + 132, CENTER + 58),
        (CENTER, CENTER + 205),
        (CENTER - 132, CENTER + 58),
        (CENTER - 38, CENTER + 58),
    ]
    draw.polygon(arrow, fill=(255, 255, 255, 255))
    draw.line(arrow + [arrow[0]], fill=DARK, width=OUTLINE, joint="curve")
    return image


def main():
    image = build_icon()
    assets = ROOT / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    image.resize((512, 512), Image.Resampling.LANCZOS).save(assets / "icon.png")

    icon_path = ROOT / "src" / "app" / "app.ico"
    image.save(
        icon_path,
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    print(f"Wrote {assets / 'icon.png'}")
    print(f"Wrote {icon_path}")


if __name__ == "__main__":
    main()
