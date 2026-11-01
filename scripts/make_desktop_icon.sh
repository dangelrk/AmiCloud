#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# App-Symbol fuer AmiCloud Desktop (macOS) aus server/logo.svg erzeugen:
# Logo auf abgerundeter Flaeche im Apple-Raster (824 px in 1024 px), dann
# alle Groessen und apps/amicloud-desktop-qt/macos/AmiCloud.icns.
#
# Aufruf (nur macOS, braucht qlmanage, sips, iconutil und ImageMagick):
#   scripts/make_desktop_icon.sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/apps/amicloud-desktop-qt/macos/AmiCloud.icns"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

qlmanage -t -s 1024 -o "$TMP" "$ROOT/server/logo.svg" >/dev/null 2>&1
LOGO="$TMP/logo.svg.png"
[ -f "$LOGO" ] || { echo "qlmanage hat kein Bild erzeugt" >&2; exit 1; }
# qlmanage malt auf Weiss: den Hintergrund von den Ecken aus durchsichtig
# machen (der Ball hat zum Rand hin einen grauen Schatten, die Fuellung
# laeuft nicht in seine weissen Felder)
magick "$LOGO" -alpha set -fuzz 3% -fill none \
    -draw "color 0,0 floodfill" -draw "color 1023,0 floodfill" \
    -draw "color 0,1023 floodfill" -draw "color 1023,1023 floodfill" \
    "$TMP/logo_clear.png"
LOGO="$TMP/logo_clear.png"

# Flaeche: 824x824 bei 100/100, Radius 185, Verlauf im Blau der Wolkenkontur
magick -size 1024x1024 xc:none \
    \( -size 824x824 gradient:'#2a3766-#141b33' \
       \( -size 824x824 xc:none -fill white -draw "roundrectangle 0,0 823,823 185,185" \) \
       -compose CopyOpacity -composite \) \
    -geometry +100+100 -compose Over -composite \
    \( "$LOGO" -resize 660x660 \) -geometry +182+170 -compose Over -composite \
    "$TMP/icon_1024.png"

SET="$TMP/AmiCloud.iconset"
mkdir "$SET"
for s in 16 32 128 256 512; do
    sips -z $s $s "$TMP/icon_1024.png" --out "$SET/icon_${s}x${s}.png" >/dev/null
    d=$((s * 2))
    sips -z $d $d "$TMP/icon_1024.png" --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$OUT"
echo "$OUT"
