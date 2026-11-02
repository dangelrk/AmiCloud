#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# AmiCloud Desktop als .dmg (macOS, Apple Silicon, ab macOS 26):
# Release-Build, macdeployqt (Qt-Frameworks ins Bundle), Ad-hoc-Signatur
# (ohne sie startet ein arm64-Programm nicht; keine Developer-ID, Plan
# 11 Nr. 4), Image mit Verweis auf /Applications.
#
# Aufruf:  scripts/make_desktop_dmg.sh      -> build/release/AmiCloud-Desktop-<version>.dmg
# Braucht: Qt 6 (Homebrew), CMake, Xcode-Kommandozeilenwerkzeuge.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build/amicloud-desktop-release"
STAGE="$ROOT/build/amicloud-desktop-dmg"
OUTDIR="$ROOT/build/release"
VERSION=$(sed -n 's/^#define AC_VERSION "\([^"]*\)".*/\1/p' \
          "$ROOT/core/libcloudcore/include/cloudcore/version.h")
APPNAME="AmiCloud Desktop"
DMG="$OUTDIR/AmiCloud-Desktop-$VERSION.dmg"

[ "$(uname -s)" = Darwin ] || { echo "nur unter macOS" >&2; exit 1; }
command -v macdeployqt >/dev/null || { echo "macdeployqt fehlt (Qt 6)" >&2; exit 1; }

# 1. Release-Build in eigenen Ordner; das Bundle landet wie immer in bin/pc/
cmake -S "$ROOT/apps/amicloud-desktop-qt" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target amicloud-desktop -j 8
APP="$ROOT/bin/pc/$APPNAME.app"

# 2. eigene Kopie fuers Image, Qt hinein, signieren
rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUTDIR"
cp -R "$APP" "$STAGE/"
macdeployqt "$STAGE/$APPNAME.app" -always-overwrite 2>&1 | grep -v "QtPdf\|QtSvg\|using QList" || true
# SVG/PDF-Plugins braucht das Programm nicht; ihre Frameworks liegen in
# anderen Homebrew-Paketen und kaemen sonst unaufgeloest mit
rm -f "$STAGE/$APPNAME.app/Contents/PlugIns/imageformats/libqsvg.dylib" \
      "$STAGE/$APPNAME.app/Contents/PlugIns/imageformats/libqpdf.dylib" \
      "$STAGE/$APPNAME.app/Contents/PlugIns/iconengines/libqsvgicon.dylib"
rmdir "$STAGE/$APPNAME.app/Contents/PlugIns/iconengines" 2>/dev/null || true
# nichts darf mehr auf Homebrew zeigen (auf einem Mac ohne Homebrew startet
# das Programm sonst nicht)
if find "$STAGE/$APPNAME.app" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
     xargs -0 otool -L 2>/dev/null | grep -q '/opt/homebrew\|/usr/local/opt'; then
    echo "Bundle verweist noch auf Homebrew:" >&2
    find "$STAGE/$APPNAME.app" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
        xargs -0 otool -L 2>/dev/null | grep '/opt/homebrew\|/usr/local/opt' >&2
    exit 1
fi
codesign --force --deep --sign - "$STAGE/$APPNAME.app"
codesign --verify --deep --strict "$STAGE/$APPNAME.app"

# 3. Image: Programm + Verweis auf Programme
ln -s /Applications "$STAGE/Programme"
rm -f "$DMG"
hdiutil create -volname "$APPNAME $VERSION" -srcfolder "$STAGE" -ov -format UDZO \
    -fs HFS+ "$DMG" >/dev/null
rm -rf "$STAGE"
echo "$DMG"
