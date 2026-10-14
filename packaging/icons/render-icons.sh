#!/bin/sh
# Regenerates the raster icons from openbrc.svg (needs rsvg-convert and ImageMagick 7).
set -e
cd "$(dirname "$0")"
for s in 16 24 32 48 64 128 256 512 1024; do
    rsvg-convert -w $s -h $s openbrc.svg -o openbrc-$s.png
done
magick openbrc-16.png openbrc-24.png openbrc-32.png openbrc-48.png openbrc-64.png openbrc-128.png openbrc-256.png openbrc.ico
python3 make_icns.py openbrc.icns
rm -f openbrc-1024.png
