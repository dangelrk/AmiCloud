# AmiCloud bauen

Diese Anleitung beschreibt alle Wege, AmiCloud aus den Quellen zu bauen:

- [Quellen holen](#quellen-holen)
- [Überblick: die make-Ziele](#überblick-die-make-ziele)
- [PC: macOS, Linux, Windows](#pc-macos-linux-windows)
- [Amiga: Cross-Compile mit vbcc (von macOS/Linux/Windows aus)](#amiga-cross-compile-mit-vbcc)
- [Amiga: nativ auf dem Amiga mit `Build-Amiga`](#amiga-nativ-mit-build-amiga)
- [Amiga-Installationsschublade (`make dist-amiga`)](#amiga-installationsschublade)
- [Testen im Emulator (FS-UAE)](#testen-im-emulator-fs-uae)
- [Server](#server)

Installation und Einrichtung stehen in [INSTALL.md](INSTALL.md).

## Quellen holen

Die gemeinsamen C-Bibliotheken liegen im Schwesterprojekt
[AmiCloudCore](https://github.com/dangelrk/AmiCloudCore) und sind als
Git-Submodul unter `core/` eingebunden. Deshalb mit Submodul klonen:

```sh
git clone --recurse-submodules https://github.com/dangelrk/AmiCloud.git
cd AmiCloud
```

Wurde ohne `--recurse-submodules` geklont, das Submodul nachholen:

```sh
git submodule update --init
```

Fehlt `core/`, bricht `make` sofort mit einem entsprechenden Hinweis ab.

## Überblick: die make-Ziele

Alle Ziele werden im Projektverzeichnis aufgerufen (`make help` zeigt
die Liste):

| Ziel | Wirkung |
|---|---|
| `make pc` (= `make`) | Kommandozeilenprogramme für den Host: `amicloud`, `amicloud-shell`, `amicloud-sync` → `bin/pc/` |
| `make qt` | Qt-6-Programme `amicloud-commander` und AmiCloud Desktop per CMake (Build-Ordner `build/<verzeichnis>/`) → `bin/pc/` |
| `make amiga` | Alle AmigaOS-Programme per vbcc-Cross-Compiler → `bin/amiga/` |
| `make dist-amiga` | Amiga-Installationsschublade `build/dist/AmiCloud/` mit Icons, dazu `build/AmiCloud.lha` |
| `make release` | wie `dist-amiga`, dazu `build/release/AmiCloud-<version>.lha` und `.readme` |
| `make build-amiga-script` | Das native Amiga-Buildskript `Build-Amiga` neu erzeugen |
| `make verify-amiga-script` | … und auf dem Host mit vbcc gegenprüfen |
| `make check-guide` | AmigaGuide-Handbuch in `docs/user/` prüfen (Knoten, Verweise, Latin-1, Zeilenlänge) |
| `make fsuae [PROGS="…"]` | Amiga-Programme im Emulator FS-UAE testen (siehe unten) |
| `make install` | Host-Programme aus `bin/pc/` nach `$(PREFIX)/bin` kopieren (`PREFIX=/usr/local`, `DESTDIR` wird unterstützt) |
| `make clean` | Alle Build-Ergebnisse entfernen (`bin/`, `lib/`, `obj/`, `build/`) |

Build-Ausgaben: Programme in `bin/pc/` bzw. `bin/amiga/`, statische
Bibliotheken (nur PC) in `lib/pc/`, Objektdateien in `obj/…` der
jeweiligen Komponente, CMake- und Dist-Ausgaben in `build/`. Alles davon
ist in `.gitignore` eingetragen.

Jedes Programm lässt sich auch einzeln bauen, das Top-Level-Makefile ruft
nur die Makefiles der Unterverzeichnisse auf:

```sh
make -C apps/amicloud -f Makefile.pc                 # PC
make -C apps/amicloud -f Makefile.amiga-cross        # Amiga (VBCC gesetzt, s. u.)
cmake -S apps/amicloud-commander-qt -B build/amicloud-commander-qt
cmake --build build/amicloud-commander-qt           # Qt
```

AmiCloud Desktop hat einen Selbsttest ohne Oberfläche gegen einen echten
Server (am besten ein leeres Testkonto; er legt `/dt_<pid>/` an):

```sh
cmake --build build/amicloud-desktop-qt --target amicloud-desktop-selftest
AC_TEST_SERVER=http://127.0.0.1:8099 AC_TEST_USER=tester AC_TEST_PASS=… \
  build/amicloud-desktop-qt/amicloud-desktop-selftest /pfad/zu/leerem/ordner
```

| Verzeichnis | Programm | `make pc` | `make qt` | `make amiga` |
|---|---|---|---|---|
| `apps/amicloud` | `amicloud` | ja | – | ja |
| `apps/amicloud-shell` | `amicloud-shell` | ja | – | ja |
| `apps/amicloud-sync` | `amicloud-sync` | ja | – | ja |
| `apps/amicloud-commander-qt` | `amicloud-commander` (Qt 6) | – | ja | – |
| `apps/amicloud-desktop-qt` | `AmiCloud Desktop.app` (Qt 6, macOS; Abgleich im Hintergrund) | – | ja | – |
| `apps/amicloud-commander-mui` | `amicloud-commander` (MUI) | – | – | ja |
| `apps/amicloud-prefs` | `AmiCloudPrefs` | – | – | ja |
| `handler/cloudhandler` | `amicloud-handler`, `amicloud-worker` | – | – | ja |

## PC: macOS, Linux, Windows

Voraussetzungen:

- C99-Compiler (gcc oder clang), GNU make, **libcurl** mit Entwicklerdateien,
  `pkg-config` (ohne pkg-config wird libcurl unter `/opt/homebrew` und
  `/usr/local` gesucht)
- nur für den Qt-Commander: **Qt 6** (Modul Widgets) und **CMake ≥ 3.16**,
  C++17-Compiler

Pakete installieren:

| System | Befehl |
|---|---|
| macOS (Homebrew) | `brew install curl pkg-config cmake qt` |
| Debian/Ubuntu | `sudo apt install build-essential pkg-config libcurl4-openssl-dev cmake qt6-base-dev` |
| Fedora/RHEL | `sudo dnf install gcc make pkgconf-pkg-config libcurl-devel cmake qt6-qtbase-devel` |

Bauen:

```sh
make pc          # amicloud, amicloud-shell, amicloud-sync  -> bin/pc/
make qt          # amicloud-commander (Qt 6)            -> bin/pc/
sudo make install                 # optional: nach /usr/local/bin
```

Getestet wurde der PC-Build auf macOS (Apple Silicon) und Windows 11
(siehe unten). Linux ist strukturell vorgesehen (reines C99/POSIX + libcurl
bzw. Qt 6), wurde aber im Rahmen dieses Projekts nicht gebaut.

### Windows (nativ mit Qt/MinGW)

Gebaut unter Windows 11 mit Qt 6.12.0 und dessen MinGW 15.1 (64 Bit). Es
entstehen native Windows-Programme; MSYS2 wird nicht gebraucht.

Voraussetzungen:

- **Qt** über den Qt Online Installer, mit „Qt 6.12 → MinGW 15.1.0 64-bit“
  sowie unter „Build Tools“ „MinGW 15.1.0 64-bit“, „CMake“ und „Ninja“
- **Git for Windows**: gebaut wird in der Git-Bash (die Makefiles brauchen
  `sh`, `sed` und `date`)
- **libcurl**, einmal aus dem Quelltext (https://curl.se/download/) mit
  demselben MinGW übersetzt: statisch und mit der Windows-eigenen TLS
  (Schannel, Zertifikate aus dem Windows-Zertifikatspeicher). Die Programme
  brauchen dann keine zusätzlichen DLLs. In der Git-Bash, mit dem `PATH`
  wie unten:

  ```sh
  cmake -S curl-8.23.0 -B build-curl -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=C:/winbuild/curl \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF \
    -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DBUILD_LIBCURL_DOCS=OFF \
    -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF -DUSE_WIN32_IDN=ON \
    -DUSE_NGHTTP2=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
    -DCURL_DISABLE_LDAP=ON -DCURL_USE_PKGCONFIG=OFF
  cmake --build build-curl && cmake --install build-curl
  ```

Bauen in der Git-Bash:

```sh
export PATH="/c/Qt/Tools/mingw1510_64/bin:/c/Qt/6.12.0/mingw_64/bin:/c/Qt/Tools/CMake_64/bin:/c/Qt/Tools/Ninja:$PATH"
export CMAKE_GENERATOR=Ninja
export CMAKE_PREFIX_PATH="C:/Qt/6.12.0/mingw_64;C:/winbuild/curl"
CURL=C:/winbuild/curl
mingw32-make pc PKG_CF="-I$CURL/include -DCURL_STATICLIB" \
  PKG_LF="-L$CURL/lib -lcurl -lnormaliz -lbcrypt -ladvapi32 -lcrypt32 -lsecur32 -lws2_32 -liphlpapi"
mingw32-make qt
```

`CMAKE_PREFIX_PATH` steht in der Umgebung statt in `QT_CMAKE_FLAGS`, weil
die Shell das Semikolon dort als Befehlstrenner lesen würde. Ergebnis:
`bin/pc/*.exe`. Die Kommandozeilenprogramme brauchen nur Windows-eigene
DLLs. Für die Qt-Programme legt `windeployqt` die Qt- und MinGW-DLLs neben
die `.exe` (am besten nach dem Kopieren in einen eigenen Ordner):

```sh
windeployqt --release --no-translations amicloud-commander.exe
```

Unter Windows suchen `amicloud`, `amicloud-shell` und `amicloud-sync` ihre
Konfiguration als `amicloud.cfg` im aktuellen Verzeichnis (oder per
`-c <datei>`).

## Amiga: Cross-Compile mit vbcc

Die Amiga-Programme werden mit dem Compiler **vbcc** für AmigaOS 3.x
(Ziel-CPU 68020, seit 0.2) übersetzt. Das ist der verifizierte Weg: alle
Amiga-Programme dieses Projekts wurden so gebaut.

### Toolchain und SDKs

Die Umgebungsvariable `VBCC` zeigt auf die Wurzel der vbcc-Installation.
Die Makefiles erwarten dort folgendes Layout:

| Pfad | Inhalt |
|---|---|
| `$VBCC/bin/` | `vc`, `vbccm68k`, `vasmm68k_mot`, `vlink` |
| `$VBCC/config/aos68k` | vc-Konfiguration für AmigaOS/68k |
| `$VBCC/targets/m68k-amigaos/` | vbcc-Target (`include/`, `lib/`) |
| `$VBCC/sdk/NDK_3.2/Include_H` | AmigaOS NDK 3.2 (C-Includes) |
| `$VBCC/sdk/AmiSSL-v5/Developer/include` | AmiSSL-5-SDK |
| `$VBCC/sdk/Roadshow/netinclude` | Roadshow-SDK (bsdsocket-Header) |
| `$VBCC/sdk/MUI_3.8/Developer/C/Include` | MUI-3.8-Entwicklerpaket (nur für den MUI-Commander) |

Bezugsquellen:

- vbcc, vasm, vlink (Quellen und Target `m68k-amigaos`):
  <http://sun.hasenbraten.de/vbcc/>
- NDK 3.2: von Hyperion Entertainment (AmigaOS 3.2 NDK)
- AmiSSL-SDK: aus den AmiSSL-Releases,
  <https://github.com/jens-maus/amissl/releases>
- Roadshow-SDK und MUI-3.8-Entwicklerpaket: Aminet

### Bauen

```sh
export VBCC=/pfad/zu/vbcc
make amiga
```

`make amiga` stellt `$VBCC/bin` selbst an den Anfang von `PATH`. Ergebnis
in `bin/amiga/`:

| Datei | Programm |
|---|---|
| `amicloud` | Kommandozeilen-Client |
| `amicloud-shell` | interaktive Shell |
| `amicloud-sync` | einmaliger Ordnerabgleich |
| `amicloud-commander` | Dateimanager (MUI) |
| `AmiCloudPrefs` | Einstellungs-Editor für `amicloud.cfg` |
| `amicloud-handler`, `amicloud-worker` | `AMICLOUD:`-Handler (zwei Prozesse) |

Auf dem Host gibt es kein `oml`; darum erzeugt der Cross-Build keine
`.lib`-Archive, sondern jedes Programm übersetzt die benötigten
Bibliotheksquellen aus `core/` direkt mit. Mathematik läuft über
`-lmieee` (der JSON-Parser rechnet mit `double`, ein 68020 hat keine FPU).

### Cross-Compile unter Windows (MSYS2) – ungetestet

Der Amiga-Build ist unter Windows nicht getestet; vorgesehen ist dieser
Weg:

1. [MSYS2](https://www.msys2.org/) installieren und in der Umgebung
   „MSYS“ die Werkzeuge holen: `pacman -S make git python`
   (`python` wird nur für `make build-amiga-script`,
   `make verify-amiga-script` und `make check-guide` gebraucht).
2. vbcc, vasm und vlink als Windows-Programme bereitstellen – aus den
   Quellen von http://sun.hasenbraten.de/vbcc/ selbst übersetzen (z. B.
   mit `gcc` aus MSYS2) oder vorkompiliert beziehen – und mit Target und
   SDKs im oben beschriebenen Layout ablegen, am besten in einem Pfad
   ohne Leerzeichen, z. B. `C:\vbcc`.
3. In der MSYS2-Shell den Pfad in MSYS2-Schreibweise angeben und bauen:

   ```sh
   export VBCC=/c/vbcc
   make amiga
   make dist-amiga
   ```

Auch hier gilt die folgende Stolperfalle: Enthält `config/aos68k`
absolute Pfade, müssen sie zum Windows-Pfad passen (sonst
`VC_CONFIG=…`).

### Stolperfalle: absolute Pfade in `aos68k`

Die Datei `$VBCC/config/aos68k` enthält bei vielen Installationen
**absolute Pfade** (z. B. auf `startup.o`). Wurde die Toolchain nach der
Installation verschoben, laufen Compiler und Assembler noch durch, aber
der Link-Schritt scheitert mit „Cannot open …/startup.o“. Lösung: die
Datei anpassen, oder eine korrigierte Kopie angeben:

```sh
make amiga VC_CONFIG=/pfad/zu/meiner/aos68k
make dist-amiga VC_CONFIG=/pfad/zu/meiner/aos68k
```

## Amiga: nativ mit `Build-Amiga`

Auf einem Amiga (oder im Emulator) mit installiertem vbcc lässt sich
AmiCloud ohne `make` bauen. Das AmigaDOS-Skript `Build-Amiga` im
Projektverzeichnis enthält alle Compiler- und Linker-Aufrufe:

```
CD AmiCloud
Execute Build-Amiga
```

Voraussetzungen auf dem Amiga:

- vbcc mit Target `m68k-amigaos`, `vc` im Suchpfad, die NDK-Includes über
  die vbcc-Konfiguration erreichbar
- AmiSSL-5-SDK, Roadshow-SDK (netinclude), MUI-3.8-Entwicklerpaket

Die SDK-Pfade stehen als Variablen am Anfang des Skripts:

```
Set AMISSL_INC "AmiSSL:Developer/include"
Set NET_INC    "NetInclude:"
Set MUI_INC    "MUI:Developer/C/Include"
```

Entweder diese Zeilen anpassen oder die Assigns `AmiSSL:`, `NetInclude:`
und `MUI:` passend setzen. Ergebnis: `bin/amiga/<Programm>`,
Objektdateien in `obj/amiga/<Programm>/`.

`Build-Amiga` ist eine **erzeugte Datei**. Sie wird mit
`make build-amiga-script` aus den `Makefile.amiga-cross`-Dateien
generiert und nicht von Hand bearbeitet. `make verify-amiga-script`
übersetzt das Skript nach `sh` und baut damit auf dem Host vollständig
gegen (alle Programme linken). Auf echter Amiga-Hardware selbst ist das
Skript **noch ungetestet**.

## Amiga-Installationsschublade

```sh
export VBCC=/pfad/zu/vbcc
make dist-amiga            # ggf. mit VC_CONFIG=... (s. o.)
```

baut zuerst alles für den Amiga und legt dann unter
`build/dist/AmiCloud/` eine fertige Installationsschublade an:

| Inhalt | Herkunft |
|---|---|
| `C/` | alle Programme aus `bin/amiga/` |
| `Docs/AmiCloud.guide` | `docs/user/AmiCloud.guide` |
| `Examples/` | `config-examples/*` |
| `Storage/DOSDrivers/AMICLOUD` | `handler/cloudhandler/mount/AMICLOUD` (über `DIST_EXTRA` im Makefile) |
| `Install-AmiCloud` (Installer), `Install-AmiCloud.shell`, `Uninstall-AmiCloud`, `ReadMe` | `install/amiga/` |
| `*.info` | erzeugt nach `install/amiga/icons.spec` |
| `LICENSE` | GPLv3-Text |

Zu allen Teilen der Schublade erzeugt `core/tools/mkicon.py` Workbench-Icons
nach `install/amiga/icons.spec` (Bild, Standardprogramm, Tooltypes, Position).
Gepackt wird mit `core/tools/lhapack.py` zu `build/AmiCloud.lha` – einem eigenen
LHA-Packer (Methode `-lh5-`, Level-2-Köpfe) in reinem Python, weil es unter
macOS keinen LHA-*Packer* gibt (Homebrews `lha` ist Lhasa und packt nur aus).
Die Archive lassen sich mit Lhasa und mit dem `lha` von AmigaOS 3.2 auspacken.

### Release

```sh
make release
```

baut `make dist-amiga` und legt in `build/release/` die Dateien für
Aminet/GitHub ab: `AmiCloud-<version>.lha` und `AmiCloud-<version>.readme` (aus
`install/amiga/aminet.readme`, `@VERSION@` wird ersetzt). Die Version steht nur in
`core/libcloudcore/include/cloudcore/version.h` (`AC_VERSION`), das Datum
ist immer der Tag des Bauens (`make` übergibt `-DAC_BUILD_DATE=TT.MM.JJJJ`,
`make release` baut alle Amiga-Programme neu). Daraus bauen alle Programme
ihre `$VER`-Angabe, und `make dist-amiga` setzt beide in die Installer-Skripte ein (`@VERSION@`,
`@DATE@`, für die Shell-Variante `@VER_MAJOR@`/`@VER_REVN@`). Weil die
Installer nur ersetzen, wenn die Version im Archiv nicht älter ist, **vor
jedem Release `AC_VERSION` erhöhen**. Ist Lhasa
installiert, prüft `lha t` das Archiv gleich mit.

Die Installation auf dem Amiga beschreibt [INSTALL.md](INSTALL.md#amiga).

## Testen im Emulator (FS-UAE)

`make fsuae` (Skript `core/tools/run-fsuae.sh`) kopiert Programme aus
`bin/amiga/` in einen Freigabeordner und startet
[FS-UAE](https://fs-uae.net/) mit einer Konfigurationsdatei. Es wird über
Umgebungsvariablen eingestellt:

| Variable | Bedeutung |
|---|---|
| `FSUAE_CONFIG` | `.fs-uae`-Konfigurationsdatei (Pflicht) |
| `FSUAE_SHARED` | Host-Ordner, in den kopiert wird (Standard: `build/fsuae-shared`); muss in der FS-UAE-Konfiguration als Laufwerk eingebunden sein, z. B. `hard_drive_1 = …` mit `hard_drive_1_label = SHARED` |
| `FSUAE_BIN` | FS-UAE-Programm; Standard: `fs-uae` aus dem `PATH`, auf macOS ersatzweise `/Applications/FS-UAE.app/Contents/MacOS/fs-uae` |

```sh
make amiga
export FSUAE_CONFIG=$HOME/fs-uae/amiga.fs-uae
export FSUAE_SHARED=$HOME/fs-uae/shared
make fsuae                                   # alle Programme
make fsuae PROGS="amicloud amicloud-shell"   # nur diese
```

Im Emulator liegen die Programme dann z. B. unter `SHARED:`. Für
Netzwerkzugriff braucht das emulierte System einen TCP/IP-Stack und
AmiSSL (siehe [INSTALL.md](INSTALL.md#voraussetzungen-amiga)).

## Server

Der Server (`server/`) ist PHP und wird nicht gebaut, sondern nur
installiert – siehe [INSTALL.md](INSTALL.md#server). Die REST-API ist in
[api/API.md](api/API.md) beschrieben, `scripts/test_api.sh` prüft einen
laufenden Server.
