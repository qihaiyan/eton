"""Generate app.ico from app.png (multi-size Windows icon).

Requires Pillow:  pip install Pillow
Run from anywhere:  python res/make_ico.py
"""

from pathlib import Path

from PIL import Image

here = Path(__file__).parent
img = Image.open(here / "app.png").convert("RGBA")
img.save(here / "app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                                  (64, 64), (128, 128), (256, 256)])
print("app.ico written")
