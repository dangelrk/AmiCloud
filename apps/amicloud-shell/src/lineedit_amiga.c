/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * lineedit_amiga.c — Rohmodus-Zeileneingabe mit History fuer AmigaOS
 * via dos.library (SetMode()/Read() auf Input()). Kompiliert nur wenn
 * PLATFORM_AMIGA gesetzt ist (Gegenstueck: lineedit_pc.c).
 *
 * VORBEHALT: Die genaue Bytefolge, die console.device im Rohmodus fuer
 * Cursor-hoch/-runter liefert, konnte hier nicht auf echter/emulierter
 * Amiga-Hardware verifiziert werden. Es werden beide laut RKM/console.device
 * gueltigen Kodierungen akzeptiert (ESC '[' 'A'/'B' sowie die 1-Byte-CSI-
 * Form 0x9B 'A'/'B'). Falls die Pfeiltasten auf echter Hardware nicht
 * reagieren, ist read_key() unten die Stelle zum Nachjustieren.
 */
#ifdef PLATFORM_AMIGA

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "lineedit.h"
#include "history.h"
#include <string.h>

typedef enum {
    KEY_CHAR, KEY_BACKSPACE, KEY_ENTER, KEY_UP, KEY_DOWN, KEY_EOF, KEY_IGNORE
} KeyKind;

static int  g_interactive = 0;
static BPTR g_in          = 0;

int lineedit_init(void)
{
    g_in = Input();
    if (!g_in || !IsInteractive(g_in)) { g_interactive = 0; return 0; }
    if (!SetMode(g_in, 1)) { g_interactive = 0; return 0; } /* 1 = raw */
    g_interactive = 1;
    return 1;
}

void lineedit_shutdown(void)
{
    if (!g_interactive) return;
    SetMode(g_in, 0); /* 0 = cooked */
    g_interactive = 0;
}

static int raw_getchar(void)
{
    UBYTE c;
    if (Read(g_in, &c, 1) != 1) return -1;
    return (int)c;
}

static void raw_putchar(char c)
{
    Write(Output(), &c, 1);
}

static void raw_puts(const char *s)
{
    Write(Output(), (APTR)s, (LONG)strlen(s));
}

/* Siehe Kopfkommentar: akzeptiert sowohl ESC '[' 'A'/'B' als auch die
 * 1-Byte-CSI-Form 0x9B 'A'/'B' fuer Cursor hoch/runter. */
static KeyKind read_key(int *ch)
{
    int c = raw_getchar();
    if (c < 0) return KEY_EOF;

    if (c == 0x1B) { /* ESC */
        int c1 = raw_getchar();
        if (c1 == '[') {
            int c2 = raw_getchar();
            if (c2 == 'A') return KEY_UP;
            if (c2 == 'B') return KEY_DOWN;
        }
        return KEY_IGNORE;
    }
    if (c == 0x9B) { /* einzelnes CSI-Byte (ISO 8859-1 C1) */
        int c2 = raw_getchar();
        if (c2 == 'A') return KEY_UP;
        if (c2 == 'B') return KEY_DOWN;
        return KEY_IGNORE;
    }
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 8 || c == 127)     return KEY_BACKSPACE;
    if (c == 4)                 return KEY_EOF; /* Strg-D */
    *ch = c;
    return KEY_CHAR;
}

static void erase_visible(int n)
{
    while (n-- > 0) raw_puts("\b \b");
}

int lineedit_readline(const char *prompt, char *buf, size_t bufsz)
{
    size_t len = 0;
    int    hist_idx   = -1;
    int    have_saved = 0;
    char   saved_current[HISTORY_LINELEN];

    if (bufsz == 0) return -1;
    buf[0] = '\0';

    raw_puts(prompt);

    for (;;) {
        int     ch = 0;
        KeyKind k  = read_key(&ch);

        switch (k) {
        case KEY_EOF:
            return -1;

        case KEY_ENTER:
            raw_putchar('\n');
            buf[len] = '\0';
            return (int)len;

        case KEY_BACKSPACE:
            if (len > 0) {
                len--;
                buf[len] = '\0';
                erase_visible(1);
            }
            break;

        case KEY_UP:
        case KEY_DOWN: {
            int hc = history_count();
            if (hc == 0) break;

            if (k == KEY_UP) {
                if (hist_idx == -1) {
                    strncpy(saved_current, buf, sizeof(saved_current) - 1);
                    saved_current[sizeof(saved_current) - 1] = '\0';
                    have_saved = 1;
                    hist_idx = hc - 1;
                } else if (hist_idx > 0) {
                    hist_idx--;
                }
            } else {
                if (hist_idx == -1) break;
                if (hist_idx < hc - 1) hist_idx++;
                else                   hist_idx = -1;
            }

            {
                const char *newtext = (hist_idx == -1)
                    ? (have_saved ? saved_current : "")
                    : history_at(hist_idx);
                size_t newlen = strlen(newtext);
                if (newlen >= bufsz) newlen = bufsz - 1;

                erase_visible((int)len);
                memcpy(buf, newtext, newlen);
                buf[newlen] = '\0';
                len = newlen;
                raw_puts(buf);
            }
            break;
        }

        case KEY_IGNORE:
            break;

        case KEY_CHAR:
        default:
            if (ch >= 0x20 && ch < 0x7F && len + 1 < bufsz) {
                buf[len++] = (char)ch;
                buf[len] = '\0';
                raw_putchar((char)ch);
            }
            break;
        }
    }
}

#endif /* PLATFORM_AMIGA */
