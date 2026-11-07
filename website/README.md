# Bebilderte Anleitung (planet-rk.de/ami/)

Anleitungen und Handbücher für AmiCloud, AmiVCS, AmiOwnCloud und AmiDevIDE als
reine HTML-Seiten. Kein PDF.

```sh
python3 website/build.py    # erzeugt website/dist/, braucht cwebp
```

- `src/*.html`: die Anleitungen, `{{v:amicloud}}` wird zur Versionsnummer und
  `{{fig:bild|Text}}` zum Bild aus `img/`.
- `build.py`: liest die Versionen aus `core/.../version.h` und `aide_version.h`,
  übernimmt die Handbücher aus den Projekten und fügt die Bilder aus
  `HANDBUCH_BILDER` ein.
- `src/counter.php`: Besucherzähler. Speichert nur die Zahl in `data/counter.txt`,
  keine IP-Adressen und keine Cookies. Gezählt werden Seitenaufrufe, auch Neuladen.

## Bei jedem neuen Release

1. `build.py` ausführen. Versionsnummern und Handbuchtexte sind damit aktuell.
2. Warnt der Bau wegen `BILDER_VERSION`: Bilder der geänderten Oberflächen neu
   aufnehmen, Zahl in `BILDER_VERSION` nachziehen.
3. `src/*.html` auf neue oder geänderte Funktionen durchsehen.
4. Hochladen wie unten.

## Hochladen

Zuerst die Archive auf Aminet hochladen, sonst laufen die Download-Links ins Leere.
Die Adressen stehen in `DOWNLOADS` in `build.py`, dort kommt später auch GitHub dazu.

Der Zählerstand liegt nur auf dem Server und darf nicht überschrieben werden:

```sh
rsync -av --delete --exclude data/counter.txt website/dist/ planetrk:/var/www/html/ami/
ssh planetrk 'touch /var/www/html/ami/data/counter.txt; chown apache:apache /var/www/html/ami/data /var/www/html/ami/data/counter.txt; chcon -R -t httpd_sys_rw_content_t /var/www/html/ami/data'
```

Danach prüfen: `https://planet-rk.de/ami/` lädt, der Zähler zeigt eine Zahl, und
`https://planet-rk.de/ami/data/counter.txt` antwortet mit 403. Antwortet es mit
200, wertet Apache die `.htaccess` dort nicht aus (`AllowOverride`).

## Bilder neu aufnehmen

Nur Demo-Daten und ein Demo-Konto verwenden, nie den Produktivserver oder echte
Zugangsdaten. Amiga: Screenshots aus dem Emulator mit den Programmen aus dem
Release-Archiv. Mac: Fenster aus dem Programm heraus rendern, Benutzerpfade
neutral halten. Browser: headless Chrome mit 1180×720.

Hilfen liegen in `tools/`:

- `demoserver.py`: Wegwerfserver aus dem Server-Paket mit Demo-Konto, Aufruf im
  jeweiligen Projektordner mit kurzem `TMPDIR`.
- `tlsproxy.py`: HTTPS davor, denn die Amiga-Programme sprechen nur HTTPS.
- `miniDav.py`: kleiner WebDAV-Server für AmiOwnCloud.
- `cdp.py`: headless Chrome ohne weitere Abhängigkeiten.
- `grab.cpp`: per `DYLD_INSERT_LIBRARIES` in Qt-Programme geladen, steuert sie und
  rendert Fenster als PNG, ohne Bildschirmfreigabe von macOS. Mit eigenem `HOME`
  und `CFFIXED_USER_HOME` starten, damit die echten Einstellungen unberührt bleiben.

Im Emulator: Beim Installer nicht „Einsteiger“ wählen, wenn nichts installiert
werden soll; dort gibt es keinen Probelauf. Der Klick auf „Weiter“ nach einem
Abbruch im Probelauf ließ den Emulator einfrieren. Bilder deshalb ohne Abbruch
aufnehmen und danach neu starten. Amiberry pausiert vermutlich ohne Fokus.
