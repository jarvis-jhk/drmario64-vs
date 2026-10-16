#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package original artwork rendered by the SDL game, without external image sources."""
from pathlib import Path
import sys
from PIL import Image
root = Path(sys.argv[1])
for suffix, size in [('p', (600, 900)), ('', (920, 430)), ('_hero', (1920, 620)),
                     ('_icon', (256, 256)), ('_logo', (1280, 320))]:
    source = root / ('steam' + suffix + '.bmp')
    with Image.open(source) as image:
        assert image.size == size, (source, image.size)
        image.save(source.with_suffix('.png'))
    source.unlink()
