/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef LINEEDIT_H
#define LINEEDIT_H

#include <stddef.h>

/*
 * lineedit.h — zeichenweise Zeileneingabe mit History-Navigation
 * (Cursor hoch/runter) fuer amicloud-shell.
 *
 * Zwei Implementierungen, wie http_pc.c/http_amiga.c:
 *   lineedit_pc.c    — macOS/Linux (termios) und Windows (conio.h)
 *   lineedit_amiga.c — AmigaOS via dos.library (SetMode + Read auf Input())
 *
 * Umfang bewusst minimal: nur Anhaengen, Backspace und History hoch/runter.
 * Keine Cursor-Links/Rechts-Bearbeitung innerhalb der Zeile.
 */

/* Schaltet den Rohmodus ein (kein OS-Echo, keine Zeilenpufferung).
 * Rueckgabe 0 bedeutet: stdin ist kein echtes interaktives Terminal
 * (Pipe/Redirect) — der Aufrufer soll dann auf den gewohnten
 * fgets()-Pfad zurueckfallen; lineedit_shutdown() ist in diesem Fall
 * ein no-op und muss trotzdem nicht aufgerufen werden. */
int lineedit_init(void);

/* Stellt den urspruenglichen Terminalzustand wieder her. Auf jedem
 * Ausstiegspfad der Shell aufrufen, wenn lineedit_init() 1 lieferte. */
void lineedit_shutdown(void);

/* Liest eine Zeile mit Backspace-Bearbeitung und History-Navigation.
 * prompt wird am Zeilenanfang ausgegeben und bei jedem Neuzeichnen
 * (History-Wechsel) erneut.
 * Rueckgabe: Zeilenlaenge (>=0, auch 0 bei leerem Enter), oder -1 bei
 * EOF (Strg-D auf leerer Zeile) — Aufrufer behandelt das wie zuvor
 * "!fgets(...)". buf wird immer NUL-terminiert. */
int lineedit_readline(const char *prompt, char *buf, size_t bufsz);

#endif /* LINEEDIT_H */
