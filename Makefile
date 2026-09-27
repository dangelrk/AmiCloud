# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# AmiCloud -- Top-Level-Makefile. "make help" zeigt alle Ziele; Details in
# docs/BUILD.md. Die eigentliche Logik liegt in core/mk/project.mk
# (AmiCloudCore-Submodul), jedes Programm hat zusaetzlich sein eigenes
# Makefile.pc / Makefile.amiga-cross / CMakeLists.txt.

PROJECT    = AmiCloud
PC_DIRS    = apps/amicloud apps/amicloud-shell apps/cloudsync
QT_DIRS    = apps/amicloud-commander-qt
AMIGA_DIRS = apps/amicloud apps/amicloud-shell apps/cloudsync apps/amicloud-commander-mui apps/amicloud-prefs handler/cloudhandler
# Zusaetzlich in die Amiga-Installationsschublade (make dist-amiga):
# DOSDriver fuer CLOUD: nach Storage/DOSDrivers/ (Install-AmiCloud kopiert
# ihn von dort nach DEVS:DOSDrivers/).
DIST_EXTRA = handler/cloudhandler/mount/CLOUD:Storage/DOSDrivers

include core/mk/project.mk
