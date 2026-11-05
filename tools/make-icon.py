"""TuxAPE's icon: Tux and his CPC, cut out of the welcome picture, its
background made transparent, at every size a desktop asks for.

    tools/make-icon.py src/app/resources/welcome.png src/app/resources/icons
"""
import sys
from collections import deque
from PIL import Image
import numpy as np

src, outdir = sys.argv[1], sys.argv[2]
im = Image.open(src).convert('RGBA')
# Tux and his CPC, in a square.
box = (36, 146, 318, 428)
crop = np.asarray(im.crop(box)).copy()
h, w = crop.shape[:2]
bg = np.array([244, 243, 244])
near = np.abs(crop[:, :, :3].astype(int) - bg).sum(2) <= 36
# The background is what is near its colour and reached from the edges:
# the belly, as white, is inside the black outline and stays.
seen = np.zeros((h, w), bool)
todo = deque([(y, x) for y in range(h) for x in (0, w - 1)] + [(y, x) for x in range(w) for y in (0, h - 1)])
while todo:
    y, x = todo.popleft()
    if seen[y, x] or not near[y, x]:
        continue
    seen[y, x] = True
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        ny, nx = y + dy, x + dx
        if 0 <= ny < h and 0 <= nx < w and not seen[ny, nx]:
            todo.append((ny, nx))
crop[seen, 3] = 0
art = Image.fromarray(crop, 'RGBA')
# Sharp: doubled by nearest, then brought to each size smoothly.
big = art.resize((w * 4, h * 4), Image.NEAREST)
for size in (512, 256, 128, 64, 48, 32, 16):
    big.resize((size, size), Image.LANCZOS).save(f'{outdir}/tuxape-{size}.png')
print('ok', w, h, 'background pixels', int(seen.sum()))
