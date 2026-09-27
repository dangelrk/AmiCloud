/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef SHELL_H
#define SHELL_H

#include "amicloud.h"

/* Runs the interactive amicloud-shell REPL until 'quit'/'exit'/'bye' or
 * EOF. cfg is the fully resolved startup configuration (already merged
 * from config file + command-line options, exactly like the plain
 * `amicloud` CLI). Returns a process exit code. */
int shell_run(const AcConfig *cfg);

#endif /* SHELL_H */
