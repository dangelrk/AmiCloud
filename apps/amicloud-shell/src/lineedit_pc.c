/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * lineedit_pc.c — Rohmodus-Zeileneingabe mit History fuer macOS/Linux
 * (termios) und Windows (conio.h). Kompiliert wenn PLATFORM_AMIGA NICHT
 * gesetzt ist (Gegenstueck: lineedit_amiga.c).
 */
#ifndef PLATFORM_AMIGA

#include "lineedit.h"
#include "history.h"

#include <stdio.h>
#include <string.h>

/* lineedit.h/history.h do not pull in amicloud.h, which sets PLATFORM_WIN */
#if defined(_WIN32) && !defined(PLATFORM_WIN)
#define PLATFORM_WIN 1
#endif

#ifdef PLATFORM_WIN
#include <conio.h>
#include <io.h>
#define AC_ISATTY(fd) _isatty(fd)
#define AC_FILENO(f)  _fileno(f)
#else
#include <unistd.h>
#include <termios.h>
#define AC_ISATTY(fd) isatty(fd)
#define AC_FILENO(f)  fileno(f)
#endif

typedef enum {
    KEY_CHAR, KEY_BACKSPACE, KEY_ENTER, KEY_UP, KEY_DOWN, KEY_EOF, KEY_IGNORE
} KeyKind;

static int g_interactive = 0;

#ifndef PLATFORM_WIN
static struct termios g_orig_termios;
#endif

int lineedit_init(void)
{
    if (!AC_ISATTY(AC_FILENO(stdin))) { g_interactive = 0; return 0; }

#ifndef PLATFORM_WIN
    {
        struct termios raw;
        if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) return 0;
        raw = g_orig_termios;
        raw.c_lflag &= ~((tcflag_t)(ICANON | ECHO));
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return 0;
    }
#endif
    g_interactive = 1;
    return 1;
}

void lineedit_shutdown(void)
{
    if (!g_interactive) return;
#ifndef PLATFORM_WIN
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
#endif
    g_interactive = 0;
}

#ifndef PLATFORM_WIN
static int raw_getchar(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    return (int)c;
}
#endif

/* Liest eine "logische" Taste. *ch ist nur bei KEY_CHAR gueltig.
 * Escape-/Sondertasten ausser Pfeil-hoch/-runter werden bewusst
 * verschluckt (KEY_IGNORE) statt ihre Rohbytes in die Zeile zu drucken. */
static KeyKind read_key(int *ch)
{
#ifdef PLATFORM_WIN
    int c = _getch();
    if (c == EOF) return KEY_EOF;
    if (c == 0 || c == 0xE0) {
        int c2 = _getch();
        if (c2 == 72) return KEY_UP;
        if (c2 == 80) return KEY_DOWN;
        return KEY_IGNORE;
    }
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 8 || c == 127)     return KEY_BACKSPACE;
    if (c == 4 || c == 26)      return KEY_EOF; /* Strg-D / Strg-Z */
    *ch = c;
    return KEY_CHAR;
#else
    int c = raw_getchar();
    if (c < 0) return KEY_EOF;
    if (c == 0x1B) { /* ESC */
        int c1 = raw_getchar();
        if (c1 == '[') {
            int c2 = raw_getchar();
            if (c2 == 'A') return KEY_UP;
            if (c2 == 'B') return KEY_DOWN;
        }
        return KEY_IGNORE; /* Home/End/PgUp/... — nicht unterstuetzt */
    }
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 8 || c == 127)     return KEY_BACKSPACE;
    if (c == 4)                 return KEY_EOF; /* Strg-D */
    *ch = c;
    return KEY_CHAR;
#endif
}

static void erase_visible(int n)
{
    while (n-- > 0) fputs("\b \b", stdout);
    fflush(stdout);
}

int lineedit_readline(const char *prompt, char *buf, size_t bufsz)
{
    size_t len = 0;
    int    hist_idx   = -1; /* -1 = eigene (evtl. angefangene) Zeile */
    int    have_saved = 0;
    char   saved_current[HISTORY_LINELEN];

    if (bufsz == 0) return -1;
    buf[0] = '\0';

    fputs(prompt, stdout);
    fflush(stdout);

    for (;;) {
        int     ch = 0;
        KeyKind k  = read_key(&ch);

        switch (k) {
        case KEY_EOF:
            return -1;

        case KEY_ENTER:
            putchar('\n');
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
                fputs(buf, stdout);
                fflush(stdout);
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
                putchar(ch);
                fflush(stdout);
            }
            break;
        }
    }
}

#endif /* !PLATFORM_AMIGA */
