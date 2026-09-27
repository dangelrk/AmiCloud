/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "history.h"
#include <string.h>

/* Klassischer Ringpuffer: g_head ist der naechste zu ueberschreibende Slot
 * (der aelteste Eintrag, sobald der Puffer voll ist). */
static char g_buf[HISTORY_MAX][HISTORY_LINELEN];
static int  g_head  = 0; /* naechster Schreib-Slot */
static int  g_count = 0; /* Anzahl belegter Slots, <= HISTORY_MAX */

void history_reset(void)
{
    g_head  = 0;
    g_count = 0;
}

void history_add(const char *line)
{
    if (!line || !*line) return;

    if (g_count > 0) {
        int last = (g_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        if (strcmp(g_buf[last], line) == 0) return; /* exakte Wiederholung */
    }

    strncpy(g_buf[g_head], line, HISTORY_LINELEN - 1);
    g_buf[g_head][HISTORY_LINELEN - 1] = '\0';

    g_head = (g_head + 1) % HISTORY_MAX;
    if (g_count < HISTORY_MAX) g_count++;
}

void history_clear(void)
{
    history_reset();
}

int history_count(void)
{
    return g_count;
}

const char *history_at(int index)
{
    int oldest_slot, slot;

    if (index < 0 || index >= g_count) return "";

    /* Aeltester belegter Slot: wenn der Puffer noch nie umgelaufen ist
     * (g_count < HISTORY_MAX), ist das Slot 0; sonst g_head (der naechste
     * zu ueberschreibende = zugleich der aelteste). */
    oldest_slot = (g_count < HISTORY_MAX) ? 0 : g_head;
    slot = (oldest_slot + index) % HISTORY_MAX;
    return g_buf[slot];
}
