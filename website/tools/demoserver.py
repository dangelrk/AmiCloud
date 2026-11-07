#!/usr/bin/env python3
"""Wegwerfserver fuer Screenshots: servertest.py mit Demo-Admin, beendet sich nie selbst."""
import sys
sys.path.insert(0, "/Users/ils/Dev/AmiCloud/core/tools")
import servertest
servertest.USER, servertest.PASS = "demo", "demo-passwort-1"
sys.argv = ["servertest.py", sys.argv[1], sys.argv[2]]
servertest.main()
