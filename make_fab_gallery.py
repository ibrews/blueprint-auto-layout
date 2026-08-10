#!/usr/bin/env python3
"""Generate Fab-marketplace-ready gallery images (>=1920x1080, <3MB, PNG) for
Blueprint Anti-Pasta from the existing, already-verified README screenshots.

Fab's media gallery requires each image to be at least 1920x1080px. The
source crops in Docs/ are real captures of actual plugin output but are all
smaller than that floor (largest is 1861x640) and the wrong aspect ratio to
just stretch. Rather than fabricate new content, this composites each real
crop onto a branded 1920x1080 canvas (same dark palette as make_social_card.py)
with a caption describing what's shown -- the graph screenshot itself is
untouched pixel data, just presented on a correctly-sized canvas.

Re-run this whenever the source Docs/*.png crops are refreshed.
"""

from PIL import Image, ImageDraw, ImageFont
import os

W, H = 1920, 1080
OUT_DIR = "Docs/fab-gallery"

BG      = (13, 17, 23)         # #0d1117 -- GitHub dark, matches make_social_card.py
PANEL   = (21, 27, 37)
ACCENT  = (0, 122, 204)        # UE blue
ACCENT2 = (0, 169, 255)
WHITE   = (255, 255, 255)
GRAY    = (140, 150, 165)
LGRAY   = (200, 210, 220)
DIM     = (80, 90, 105)

FONT = "/System/Library/Fonts/SFNS.ttf"
MONO = "/System/Library/Fonts/SFNSMono.ttf"


def load_font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()


def make_gallery_image(src_path, out_path, caption, max_upscale=1.6):
    src = Image.open(src_path).convert("RGB")
    sw, sh = src.size

    canvas = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(canvas)

    # top/bottom accent bars, matching the social card's brand treatment
    draw.rectangle([(0, 0), (W, 4)], fill=ACCENT2)
    draw.rectangle([(0, H - 4), (W, H)], fill=ACCENT)

    # reserve top for wordmark, bottom for caption; image lives in between
    top_margin, bottom_margin, side_margin = 96, 140, 100
    content_w = W - 2 * side_margin
    content_h = H - top_margin - bottom_margin

    scale = min(content_w / sw, content_h / sh, max_upscale)
    nw, nh = int(sw * scale), int(sh * scale)
    resized = src.resize((nw, nh), Image.LANCZOS)

    # centered panel behind the image for a little breathing room
    pad = 24
    panel_box = [
        (W - nw) // 2 - pad,
        top_margin + (content_h - nh) // 2 - pad,
        (W - nw) // 2 + nw + pad,
        top_margin + (content_h - nh) // 2 + nh + pad,
    ]
    draw.rounded_rectangle(panel_box, radius=10, fill=PANEL)
    canvas.paste(resized, ((W - nw) // 2, top_margin + (content_h - nh) // 2))

    # wordmark, top-left
    f_word = load_font(FONT, 30)
    draw.text((side_margin, 34), "Blueprint", font=f_word, fill=WHITE)
    bbox = draw.textbbox((side_margin, 34), "Blueprint", font=f_word)
    draw.text((bbox[2] + 12, 34), "Anti-Pasta", font=f_word, fill=ACCENT2)

    # caption, bottom, centered
    f_cap = load_font(FONT, 30)
    cbbox = draw.textbbox((0, 0), caption, font=f_cap)
    cw = cbbox[2] - cbbox[0]
    draw.text(((W - cw) // 2, H - bottom_margin + 34), caption, font=f_cap, fill=LGRAY)

    os.makedirs(OUT_DIR, exist_ok=True)
    canvas.save(out_path, "PNG", optimize=True)
    size_kb = os.path.getsize(out_path) / 1024
    print(f"Saved {out_path}  ({W}x{H}, {size_kb:.0f} KB)")


if __name__ == "__main__":
    make_gallery_image(
        "Docs/before-after.png",
        f"{OUT_DIR}/01-before-after.png",
        "Straight execution spines, pin-aligned data wires -- one click, one undo",
    )
    make_gallery_image(
        "Docs/cleanup.png",
        f"{OUT_DIR}/02-cleanup.png",
        "Messy graph in, readable layered flow out",
    )
    make_gallery_image(
        "Docs/grouped-colored.png",
        f"{OUT_DIR}/03-grouped-colored.png",
        "Auto-grouped into named, keyword-colored comment boxes",
    )
