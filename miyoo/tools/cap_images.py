#!/usr/bin/env python3
"""Cap the size of the few huge images the Miyoo Mini Plus can't afford.

A 3000x3000 star background decodes to 36 MB and takes seconds to decode on the
device. These are soft images (nebulas, the galaxy map), so they ship smaller; the
game draws them at the size saved in sizes.txt (Sprite::AddFrames keeps it), just
softer. Run after sizes.txt is written: every capped image must already have its
original size there, or the game would draw it at the capped size.

    cap_images.py <images folder> <sizes.txt>
"""
import os
import re
import sys

from PIL import Image

# Longest side allowed, by sprite name prefix (first match wins).
CAPS = [('ui/milky way', 1280), ('star/', 1024)]
# Frame and blending markers a file name may carry after the sprite name.
SUFFIX = re.compile(r'(@[12]x)?([-+~=^]\d*)?$')


def sprite_name(relative):
    return SUFFIX.sub('', os.path.splitext(relative)[0])


def main():
    images, sizes_path = sys.argv[1], sys.argv[2]
    sizes = {}
    for line in open(sizes_path):
        parts = line.rstrip('\n').split('\t')
        if len(parts) == 3:
            sizes[parts[0]] = (int(parts[1]), int(parts[2]))
    changed = 0
    for root, _, files in os.walk(images):
        for file in sorted(files):
            path = os.path.join(root, file)
            relative = os.path.relpath(path, images)
            name = sprite_name(relative)
            cap = next((c for prefix, c in CAPS if name.startswith(prefix)), None)
            if not cap:
                continue
            with Image.open(path) as image:
                width, height = image.size
                if max(width, height) <= cap:
                    continue
                assert name in sizes, f'{name} has no saved size; write sizes.txt first'
                scale = cap / max(width, height)
                small = image.resize((max(2, round(width * scale)), max(2, round(height * scale))), Image.LANCZOS)
            if path.lower().endswith(('.jpg', '.jpeg')):
                small.convert('RGB').save(path, quality=92)
            else:
                small.save(path, optimize=True)
            print(f'{relative}: {width}x{height} -> {small.size[0]}x{small.size[1]} (drawn at {sizes[name]})')
            changed += 1
    print(f'{changed} images capped')


if __name__ == '__main__':
    main()
