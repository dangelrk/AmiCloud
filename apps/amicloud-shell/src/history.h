/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef HISTORY_H
#define HISTORY_H

/*
 * history.h — reiner In-Memory-Ringpuffer fuer die letzten Shell-Eingaben.
 *
 * Plattformunabhaengig (keine Datei-I/O!): Die History lebt ausschliesslich
 * im Prozessspeicher und wird beim Beenden der Shell verworfen — das ist
 * bewusst so und keine Einschraenkung, die spaeter "nachgeruestet" werden
 * soll. Wer History ueber Sitzungen hinweg will, muesste explizit
 * Datei-I/O ergaenzen; das ist hier nicht vorgesehen.
 */

#define HISTORY_MAX     100
#define HISTORY_LINELEN 512

/* Leert die History. Einmal beim Start von shell_run() aufrufen. */
void history_reset(void);

/* Fuegt eine Zeile hinzu. Leere Zeilen und exakte Wiederholungen der
 * zuletzt gespeicherten Zeile werden stillschweigend uebersprungen.
 * Laengere Zeilen als HISTORY_LINELEN-1 werden abgeschnitten. */
void history_add(const char *line);

/* Backing fuer den `history clear`-Befehl. */
void history_clear(void);

int history_count(void);

/* index 0 = aeltester noch gespeicherter Eintrag, count()-1 = neuester.
 * Ausserhalb des gueltigen Bereichs wird "" zurueckgegeben. */
const char *history_at(int index);

#endif /* HISTORY_H */
