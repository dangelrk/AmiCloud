#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# Kopiert gebaute AmigaOS-Programme aus bin/amiga/ in einen Freigabeordner
# des Emulators FS-UAE und startet FS-UAE mit einer Konfigurationsdatei.
# Beschrieben in docs/BUILD.md ("Testen im Emulator").
#
# Aufruf:   scripts/run-fsuae.sh [programm ...]
# Ohne Argumente werden ALLE Dateien aus bin/amiga/ kopiert, mit Argumenten
# nur die genannten (z.B. `scripts/run-fsuae.sh amicloud-commander amicloud-handler`).
#
# Voraussetzung: die Programme sind gebaut (`make amiga`).
#
# Konfiguration ueber Umgebungsvariablen:
#   FSUAE_CONFIG  Pfad zur .fs-uae-Konfigurationsdatei (Pflicht). Darin den
#                 Freigabeordner als Laufwerk einbinden, z.B.
#                   hard_drive_1 = /pfad/zum/freigabeordner
#                   hard_drive_1_label = SHARED
#   FSUAE_SHARED  Freigabeordner auf dem Host, in den kopiert wird (Pflicht);
#                 muss derselbe Ordner sein, den FSUAE_CONFIG einbindet.
#   FSUAE_BIN     FS-UAE-Programm. Standard: "fs-uae" aus dem PATH, auf
#                 macOS ersatzweise /Applications/FS-UAE.app/Contents/MacOS/fs-uae
#
# Beispiel:
#   FSUAE_CONFIG=$HOME/fs-uae/amiga.fs-uae FSUAE_SHARED=$HOME/fs-uae/shared \
#       scripts/run-fsuae.sh
# Im Amiga dann z.B.:  CD SHARED:  und  amicloud version

set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AMIGA_BIN_DIR="$PROJECT_DIR/bin/amiga"

: "${FSUAE_CONFIG:?FSUAE_CONFIG nicht gesetzt (Pfad zur .fs-uae-Konfiguration, siehe Kopf dieses Skripts)}"
: "${FSUAE_SHARED:?FSUAE_SHARED nicht gesetzt (Host-Freigabeordner, siehe Kopf dieses Skripts)}"

if [ -z "${FSUAE_BIN:-}" ]; then
    if command -v fs-uae >/dev/null 2>&1; then
        FSUAE_BIN="$(command -v fs-uae)"
    elif [ -x /Applications/FS-UAE.app/Contents/MacOS/fs-uae ]; then
        FSUAE_BIN=/Applications/FS-UAE.app/Contents/MacOS/fs-uae
    else
        echo "FS-UAE nicht gefunden -- FSUAE_BIN auf das fs-uae-Programm setzen." >&2
        exit 1
    fi
fi

[ -x "$FSUAE_BIN" ] || { echo "FS-UAE nicht ausfuehrbar: $FSUAE_BIN" >&2; exit 1; }
[ -f "$FSUAE_CONFIG" ] || { echo "FS-UAE-Config nicht gefunden: $FSUAE_CONFIG" >&2; exit 1; }

mkdir -p "$FSUAE_SHARED"

if [ "$#" -gt 0 ]; then
    for name in "$@"; do
        src="$AMIGA_BIN_DIR/$name"
        [ -f "$src" ] || { echo "Nicht gefunden: $src (erst 'make amiga' ausfuehren)" >&2; exit 1; }
        cp "$src" "$FSUAE_SHARED/"
        echo "Kopiert: $src -> $FSUAE_SHARED/$name"
    done
else
    [ -d "$AMIGA_BIN_DIR" ] && [ -n "$(ls -A "$AMIGA_BIN_DIR" 2>/dev/null)" ] || { echo "Keine Programme in $AMIGA_BIN_DIR gefunden (erst 'make amiga' ausfuehren)." >&2; exit 1; }
    for src in "$AMIGA_BIN_DIR"/*; do
        [ -f "$src" ] || continue
        cp "$src" "$FSUAE_SHARED/"
        echo "Kopiert: $src -> $FSUAE_SHARED/$(basename "$src")"
    done
fi

echo "Starte FS-UAE ($FSUAE_CONFIG) ..."
exec "$FSUAE_BIN" "$FSUAE_CONFIG"
