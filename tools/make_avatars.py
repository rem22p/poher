#!/usr/bin/env python3
"""Prepare the avatar set of the game from the pictures in refs/.

Every picture is cut to the square around the face (the squares are listed in
mockups/avatars.py), turned upright, shrunk to 256 x 256 and saved as
avatars/NN.jpg. NN is the number of the picture on the sheet that
`python3 mockups/avatars.py` shows: 01, 02 and so on in the order of the file
names.

  tools/make_avatars.py          write avatars/
  tools/make_avatars.py FOLDER   write somewhere else

Needs ImageMagick (`magick`).
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "mockups"))
import avatars as sheet  # noqa: E402  (mockups/avatars.py)

SIDE = 256


def main(argv):
    dest = Path(argv[1]) if len(argv) > 1 else ROOT / "avatars"
    dest.mkdir(parents=True, exist_ok=True)
    pictures = sheet.pictures()
    for number, path in enumerate(pictures, start=1):
        out = dest / f"{number:02d}.jpg"
        subprocess.run(
            ["magick", str(path), *sheet.crop_args(path), "-filter", "Lanczos", "-resize", f"{SIDE}x{SIDE}!",
             "-strip", "-quality", "92", str(out)],
            check=True,
        )
    print(f"{dest}: {len(pictures)} avatars")


if __name__ == "__main__":
    main(sys.argv)
