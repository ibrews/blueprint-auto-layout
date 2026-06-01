#!/usr/bin/env python3
"""Generate the 1280×640 GitHub social preview card for Blueprint Auto Layout."""

from PIL import Image, ImageDraw, ImageFont, ImageFilter
import os, sys

W, H = 1280, 640
OUT = "Docs/social-card.png"

BG      = (13, 17, 23)         # #0d1117 — GitHub dark
PANEL   = (21, 27, 37)         # slightly lighter for left panel
ACCENT  = (0, 122, 204)        # UE blue
ACCENT2 = (0, 169, 255)        # lighter UE blue for highlights
WHITE   = (255, 255, 255)
GRAY    = (140, 150, 165)
LGRAY   = (200, 210, 220)
DIM     = (80, 90, 105)

FONT    = "/System/Library/Fonts/SFNS.ttf"
MONO    = "/System/Library/Fonts/SFNSMono.ttf"

def load_font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()

# ── canvas ────────────────────────────────────────────────────────────────────
card = Image.new("RGB", (W, H), BG)
draw = ImageDraw.Draw(card)

# ── right panel: "after" half of cleanup.png ─────────────────────────────────
from PIL import ImageEnhance

src = Image.open("Docs/cleanup.png").convert("RGB")
src_w, src_h = src.size

# The right half is the "after" — clean graph; crop top labels (ends ~y=168)
after_x = src_w // 2
after = src.crop((after_x + 40, 168, src_w - 8, src_h - 8))

# Boost brightness so nodes pop on the dark social card
after = ImageEnhance.Brightness(after).enhance(1.5)
after = ImageEnhance.Contrast(after).enhance(1.15)

# Zoom in 1.9× on the densest node cluster (left ~55% width, lower 80% height)
aw, ah = after.size
focus = after.crop((0, int(ah * 0.12), int(aw * 0.58), ah))

# Scale the focus region to fill the right 740px × 640px panel
panel_w, panel_h = 740, H
scale = max(panel_w / focus.width, panel_h / focus.height)
nw, nh = int(focus.width * scale), int(focus.height * scale)
focus_r = focus.resize((nw, nh), Image.LANCZOS)
after_crop = focus_r.crop((0, 0, panel_w, panel_h))

card.paste(after_crop, (W - panel_w, 0))

# ── gradient vignette: short blend at the seam ───────────────────────────────
SEAM = W - panel_w          # x=540
FADE = 90                   # narrow fade width
grad = Image.new("RGBA", (FADE, H), (0, 0, 0, 0))
gdraw = ImageDraw.Draw(grad)
for x in range(FADE):
    t = 1.0 - (x / FADE)
    alpha = int(255 * (t ** 2.2))   # sharper fade
    r, g, b = BG
    gdraw.line([(x, 0), (x, H)], fill=(r, g, b, alpha))

card.paste(Image.new("RGB", (SEAM, H), BG), (0, 0))
card.paste(grad, (SEAM, 0), grad)

# ── top accent bar ─────────────────────────────────────────────────────────────
draw.rectangle([(0, 0), (W, 3)], fill=ACCENT2)

# ── Blueprint node motif (decorative dots/lines top-left) ────────────────────
# Small "node" hint — a subtle row of UE-style connector pins
for i, (x, y, r, col) in enumerate([
    (38, 54, 6, ACCENT),
    (38, 80, 6, ACCENT2),
    (38, 106, 6, DIM),
    (38, 132, 6, DIM),
]):
    draw.ellipse([(x - r, y - r), (x + r, y + r)], fill=col)
    # horizontal wire tick
    draw.line([(x + r, y), (x + r + 16, y)], fill=col, width=2)

# Vertical spine connecting them
draw.line([(38, 54), (38, 132)], fill=DIM, width=2)

# ── left panel text ────────────────────────────────────────────────────────────
LEFT = 72

# Plugin name — big two-line treatment
f_title_big  = load_font(FONT, 78)
f_title_sub  = load_font(FONT, 78)
f_tag        = load_font(FONT, 26)
f_feature    = load_font(FONT, 22)
f_mono       = load_font(MONO, 20)
f_version    = load_font(MONO, 18)

# "Blueprint" — white
draw.text((LEFT, 64), "Blueprint", font=f_title_big, fill=WHITE)
# "Auto Layout" — UE blue
draw.text((LEFT, 150), "Auto Layout", font=f_title_sub, fill=ACCENT2)

# Tagline
draw.text((LEFT, 258), "Sugiyama engine for Unreal Blueprint graphs", font=f_tag, fill=GRAY)

# Separator line
draw.line([(LEFT, 302), (LEFT + 390, 302)], fill=DIM, width=1)

# Feature bullets
features = [
    ("Layered ranking + crossing minimisation",),
    ("Pin-aware straight execution spines",),
    ("Knot-routed long edges",),
    ("Auto-grouped comment boxes",),
]
fy = 320
for (feat,) in features:
    # bullet dot
    draw.ellipse([(LEFT, fy + 8), (LEFT + 6, fy + 14)], fill=ACCENT2)
    draw.text((LEFT + 16, fy), feat, font=f_feature, fill=LGRAY)
    fy += 36

# Keyboard shortcut badge
badge_x, badge_y = LEFT, fy + 18
badge_text = "Ctrl + Shift + L"
# measure
bbox = draw.textbbox((0, 0), badge_text, font=f_mono)
bw = bbox[2] - bbox[0] + 28
bh = bbox[3] - bbox[1] + 14
draw.rounded_rectangle(
    [(badge_x, badge_y), (badge_x + bw, badge_y + bh)],
    radius=6,
    outline=ACCENT,
    width=1,
    fill=(ACCENT[0], ACCENT[1], ACCENT[2], 30),
)
# tint fill
tint = Image.new("RGBA", (bw, bh), (*ACCENT, 25))
card_rgba = card.convert("RGBA")
card_rgba.paste(tint, (badge_x, badge_y), tint)
card = card_rgba.convert("RGB")
draw = ImageDraw.Draw(card)
draw.rounded_rectangle([(badge_x, badge_y), (badge_x + bw, badge_y + bh)], radius=6, outline=ACCENT, width=1)
draw.text((badge_x + 14, badge_y + 7), badge_text, font=f_mono, fill=ACCENT2)

# "→ instant clean graph" after the badge
arrow_x = badge_x + bw + 12
draw.text((arrow_x, badge_y + 7), "→ one-pass clean graph", font=f_mono, fill=GRAY)

# version tag bottom-left
f_ver = load_font(FONT, 17)
draw.text((LEFT, H - 34), "v0.6.4  ·  UE 5.4+  ·  github.com/ibrews/blueprint-auto-layout",
          font=f_ver, fill=DIM)

# ── bottom accent bar ──────────────────────────────────────────────────────────
draw.rectangle([(0, H - 3), (W, H)], fill=ACCENT)

# ── save ───────────────────────────────────────────────────────────────────────
card.save(OUT, "PNG", optimize=True)
print(f"Saved {OUT}  ({W}×{H})")
