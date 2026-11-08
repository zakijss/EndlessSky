#!/usr/bin/env python3
"""Shrink every image under images/ for the Miyoo Mini Plus build.

The device draws the game's logical 1024x768 screen at 640x480 (scale 0.625),
so no image is ever shown larger than 0.625 of its original size. The engine
multiplies sprite sizes back by 1/SCALE, so gameplay dimensions are unchanged.

    shrink_images.py <src images dir> <dst dir> [scale]

Art that is always drawn at its own size (landscapes, scenes, thumbnails, outfit
pictures, menu and interface art) is better made at 0.8, the Miyoo's 800x600 scale,
so it lands pixel for pixel. Sprite sizes come from sizes.txt (Sprite::AddFrames keeps
them), which must be written from the 0.625 art first; then run this again at 0.8 on
those folders (FIXED_SIZE), and finally cap_images.py.
"""
import os, shutil, sys
from concurrent.futures import ProcessPoolExecutor
from PIL import Image

KEEP = {"font"}  # glyph atlases are copied at full size; the renderer filters them itself
# Drawn only at their own size: ship at 0.8 (see above).
FIXED_SIZE = {"land", "scene", "thumbnail", "outfit", "ui", "_menu", "icon"}

def shrink(job):
    src, dst, scale = job
    if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
        return 0
    if scale == 1.0:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        return 1
    im = Image.open(src)
    # Even sizes keep the sprite centre on a pixel boundary, as upstream requires of its art.
    even = lambda v: max(2, 2 * round(v * scale / 2))
    w, h = even(im.width), even(im.height)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if src.lower().endswith((".jpg", ".jpeg")):
        im.convert("RGB").resize((w, h), Image.LANCZOS).save(dst, quality=88)
    else:
        # Resample premultiplied so transparent pixels don't bleed dark edges.
        im = im.convert("RGBA").convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")
        im.save(dst, optimize=False, compress_level=6)
    return 1

def main():
    src_root, dst_root = sys.argv[1], sys.argv[2]
    scale = float(sys.argv[3]) if len(sys.argv) > 3 else 0.625
    jobs = []
    for d, _, files in os.walk(src_root):
        rel = os.path.relpath(d, src_root)
        keep = rel.split(os.sep)[0] in KEEP
        for f in files:
            jobs.append((os.path.join(d, f), os.path.join(dst_root, rel, f), 1.0 if keep else scale))
    with ProcessPoolExecutor() as ex:
        done = sum(ex.map(shrink, jobs, chunksize=16))
    print(f"{done} written, {len(jobs) - done} up to date")

if __name__ == "__main__":
    main()
