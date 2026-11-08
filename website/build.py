#!/usr/bin/env python3
"""Baut die bebilderte Anleitung nach website/dist/.

    python3 website/build.py

Die Versionsnummern stehen an genau einer Stelle: Sie werden hier aus den
Quellen der Programme gelesen. Die Handbuchtexte kommen ebenfalls aus den
Projekten; nur die Bilder werden an den Stellen aus HANDBUCH_BILDER eingefuegt.
Was bei einem neuen Release zu tun ist, steht in website/README.md.
"""
import html
import re
import shutil
import subprocess
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parent
DEV = WEB.parent.parent
SRC = WEB / "src"
IMG = WEB / "img"
DIST = WEB / "dist"

# Fuer welche Programmversion die Bilder aufgenommen wurden. Nach neuen
# Aufnahmen hier nachziehen; solange die Zahl abweicht, warnt der Bau.
BILDER_VERSION = {"amicloud": "0.10", "amivcs": "0.10", "amiowncloud": "0.10", "amidevide": "0.5"}

PROJEKTE = {
    "amicloud": ("AmiCloud", "Eigener Cloud-Speicher"),
    "amivcs": ("AmiVCS", "Versionsverwaltung"),
    "amiowncloud": ("AmiOwnCloud", "ownCloud und Nextcloud"),
    "amidevide": ("AmiDevIDE", "Entwicklungsumgebung"),
}

# Vorerst nicht in Suchmaschinen: nur per Direktlink erreichbar. Zum Freischalten
# auf False setzen, neu bauen und hochladen.
NOINDEX = False

# Woher die Programme kommen. Eine Stelle, damit der spaetere Wechsel oder die
# Ergaenzung um GitHub einfach bleibt. Die Namen auf Aminet tragen keine Version.
DOWNLOADS = {
    "amicloud": "https://aminet.net/package/comm/net/AmiCloud",
    "amivcs": "https://aminet.net/package/dev/misc/AmiVCS",
    "amiowncloud": "https://aminet.net/package/comm/net/AmiOwnCloud",
    "amidevide": "https://aminet.net/package/dev/misc/AmiDevIDE",
}
# Quellen und Releases (auch die Fassungen fuer Mac und PC) auf GitHub;
# {{gh:projekt}} in den Seiten.
GITHUB = {
    "amicloud": "https://github.com/dangelrk/AmiCloud",
    "amivcs": "https://github.com/dangelrk/AmiVCS",
    "amiowncloud": "https://github.com/dangelrk/AmiOwnCloud",
    "amidevide": "https://github.com/dangelrk/AmiDevIDE",
}

# Handbuch: Anker -> [(Bild, Bildunterschrift)]. Das Bild steht hinter dem
# ersten Absatz des Kapitels.
HANDBUCH_BILDER = {
    "amicloud": {
        "installation": [("ac-install-modus", "Der Installer fragt zuerst nach dem Installationsmodus."),
                         ("ac-install-teile", "Im Modus „Geübter Benutzer“ lassen sich die Teile einzeln wählen.")],
        "webui": [("ac-web-dateien", "Die Dateiansicht der Weboberfläche."),
                  ("ac-web-suche", "Die Suche findet Dateien nach Muster, hier *.txt."),
                  ("ac-web-zuletzt", "„Zuletzt geändert“ zeigt die neuesten Dateien."),
                  ("ac-web-freigaben", "Die Übersicht der Freigaben."),
                  ("ac-web-benutzer", "Die Benutzerverwaltung für Administratoren.")],
        "apppw": [("ac-web-einstellungen", "Unter „Einstellungen“ legst du je Gerät ein eigenes App-Passwort an.")],
        "prefs": [("ac-prefs", "AmiCloudPrefs auf der Workbench.")],
        "shell": [("ac-shell", "amicloud-shell: Ordner wechseln und Dateien holen.")],
        "desktop": [("ac-desk-1-server", "AmiCloud Desktop: Der Assistent fragt zuerst nach dem Server."),
                    ("ac-desk-4-auswahl", "Ordnerauswahl im Assistenten."),
                    ("ac-desk-einst-konto", "Einstellungen: das Konto."),
                    ("ac-desk-einst-ordner", "Einstellungen: lokaler Ordner und Server-Ordner."),
                    ("ac-desk-einst-abgleich", "Abgleich-Einstellungen mit der Rückfrage vor Massenlöschungen."),
                    ("ac-desk-aktivitaet", "Das Aktivitätsfenster protokolliert jeden Abgleich.")],
        "commander": [("ac-cmd-mui", "Der Commander auf dem Amiga (MUI)."),
                      ("ac-qt-commander", "Der Commander auf dem Mac (Qt)."),
                      ("ac-qt-suche", "Die Suche des Commanders auf dem Mac.")],
        "handler": [("ac-handler", "AMICLOUD: als Laufwerk auf der Workbench.")],
    },
    "amivcs": {
        "installation": [("vc-install", "Der Installer von AmiVCS.")],
        "prefs": [("vc-config", "AmiVCSConfig: Name und E-Mail für alle Repositories, Remote-URL und Benutzer für dieses.")],
        "gui-mui": [("vc-mui", "amivcs-gui auf dem Amiga (MUI).")],
        "gui-qt": [("vc-qt-haupt", "amivcs-gui auf dem Mac (Qt) mit grafischem Commit-Graphen."),
                   ("vc-qt-branch", "Einen neuen Branch anlegen.")],
        "webui": [("vc-web-repos", "Die eigenen Repositories in der Weboberfläche."),
                  ("vc-web-code", "Dateibaum und README eines Repositorys."),
                  ("vc-web-blob", "Dateiansicht mit Zeilennummern."),
                  ("vc-web-history", "Die Historie mit Commit-Graph."),
                  ("vc-web-diff", "Ein Commit mit den zeilenweisen Änderungen."),
                  ("vc-web-branches", "Die Branches eines Repositorys."),
                  ("vc-web-repo-settings", "Einstellungen eines Repositorys: Beschreibung, öffentlich oder privat, Löschen."),
                  ("vc-web-einstellungen", "Persönliche Einstellungen mit den App-Passwörtern.")],
    },
    "amiowncloud": {
        "installation": [("oc-install", "Der Installer von AmiOwnCloud.")],
        "handler": [("oc-handler", "OWNCLOUD: als Laufwerk auf der Workbench.")],
        "owncloudprefs": [("oc-prefs", "AmiOwnCloudPrefs: die Einstellungen des Laufwerks.")],
        "fenster": [("oc-sync-fenster", "Das Ordnerpaar-Fenster der Commodity.")],
        "amiowncloudsyncprefs": [("oc-syncprefs", "AmiOwnCloudSyncPrefs: Profil und Ordnerpaare.")],
    },
    "amidevide": {
        "node-hauptfenster": [("ide-mui", "AmigaIDE auf dem Amiga (MUI)."),
                              ("ide-qt-haupt", "Die kommende Fassung für den Mac nach einem erfolgreichen Build.")],
        "node-projekteinstellungen": [("ide-qt-projekt", "Projekteinstellungen in der kommenden Fassung für den Mac.")],
        "node-einstellungen": [("ide-qt-einstellungen", "Globale Einstellungen in der kommenden Fassung für den Mac.")],
        "node-versionen": [("ide-qt-vcs", "Versionsverwaltung mit AmiVCS in der kommenden Fassung für den Mac.")],
    },
}

fehlende_bilder = set()


def lies_versionen():
    core = (DEV / "AmiCloud/core/libcloudcore/include/cloudcore/version.h").read_text()
    m = re.search(r'#define\s+AC_VERSION(?:_STRING)?\s+"([0-9.]+)"', core)
    if not m:
        major = re.search(r"#define\s+AC_VERSION_MAJOR\s+(\d+)", core)
        minor = re.search(r"#define\s+AC_VERSION_MINOR\s+(\d+)", core)
        if not (major and minor):
            sys.exit("version.h: keine AC_VERSION gefunden")
        kern = f"{major.group(1)}.{minor.group(1)}"
    else:
        kern = m.group(1)
    ide_h = next((DEV / "AmiDevIDE/ide/core").rglob("aide_version.h")).read_text()
    mi = re.search(r'#define\s+AMIGAIDE_VERSION_STRING\s+"([0-9.]+)"', ide_h)
    if not mi:
        sys.exit("aide_version.h: keine AMIGAIDE_VERSION_STRING gefunden")
    return {"amicloud": kern, "amivcs": kern, "amiowncloud": kern, "amidevide": mi.group(1)}


def bild_html(name, unterschrift, prefix=""):
    png = IMG / f"{name}.png"
    if not png.exists():
        fehlende_bilder.add(name)
        return f"<!-- Bild fehlt: {name} -->"
    amiga = not name.startswith(("ac-web", "vc-web", "ac-desk", "ac-qt", "vc-qt", "ide-qt"))
    kleiner = 700 if amiga else 860
    breite, hoehe = bild_groesse(png)
    # Retina-Aufnahmen doppelt so gross wie angezeigt speichern.
    if name.startswith(("ac-qt", "ac-desk", "vc-qt", "ide-qt")):
        anzeige = min(kleiner, breite // 2 if breite > 1000 else breite)
    elif amiga:
        anzeige = breite if breite >= 400 else breite * 2
    else:
        anzeige = min(kleiner, breite)
    hoch = round(hoehe * anzeige / breite)
    ziel = f"{name}.webp" if not amiga else f"{name}.png"
    klasse = ' class="amiga"' if amiga else ""
    art = {"ac-web": "Browser", "vc-web": "Browser", "ac-desk": "Mac", "ac-qt": "Mac", "vc-qt": "Mac", "ide-qt": "Mac"}
    bereich = next((v for k, v in art.items() if name.startswith(k)), "Amiga")
    text = html.escape(unterschrift, quote=False)
    alt = html.escape(unterschrift, quote=True)
    return (f'<figure><a href="{prefix}img/{ziel}"><img{klasse} src="{prefix}img/{ziel}" width="{anzeige}" '
            f'height="{hoch}" loading="lazy" alt="{alt}"></a>'
            f'<figcaption><span class="bereich">{bereich}</span>{text}</figcaption></figure>')


def bild_groesse(png):
    with open(png, "rb") as f:
        kopf = f.read(24)
    return int.from_bytes(kopf[16:20], "big"), int.from_bytes(kopf[20:24], "big")


def kopf(titel, aktiv, beschreibung):
    nav = [("index", "Übersicht")] + [(k, v[0]) for k, v in PROJEKTE.items()]
    links = "".join(f'<a href="{k if k != "index" else "index"}.html"' + (' aria-current="page"' if k == aktiv else "")
                    + f">{t}</a>" for k, t in nav if k != "index")
    return f"""<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(titel)}</title>
<meta name="description" content="{html.escape(beschreibung, quote=True)}">
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<nav class="ami-bar" aria-label="Hauptnavigation"><div class="ami-bar-in"><span class="ami-home"><a href="/">planet-RK</a> › <a href="index.html">AMIGA-Tools · Anleitung</a></span>{links}</div></nav>
"""


def fuss(versionen):
    stand = " · ".join(f"{PROJEKTE[k][0]} {v}" for k, v in versionen.items())
    return f"""<footer class="fuss"><div class="fuss-in">
<span>Stand: {stand}</span>
<span><a href="/impressum.html">Impressum</a> · <a href="datenschutz.html">Datenschutz</a> · <a href="/disclaimer.html">Disclaimer</a></span>
<span class="zaehler">Seitenaufrufe <img src="counter.php" width="90" height="22" alt="Zähler"></span>
</div></footer>
</body>
</html>
"""


def ersetze(text, versionen):
    def fig(m):
        return bild_html(m.group(1).strip(), m.group(2).strip())
    text = re.sub(r"\{\{fig:([^|}]+)\|([^}]*)\}\}", fig, text)
    text = re.sub(r"\{\{dl:(\w+)\}\}", lambda m: DOWNLOADS[m.group(1)], text)
    text = re.sub(r"\{\{gh:(\w+)\}\}", lambda m: GITHUB[m.group(1)], text)
    return re.sub(r"\{\{v:(\w+)\}\}", lambda m: versionen[m.group(1)], text)


def baue_seite(name, versionen):
    quelle = (SRC / f"{name}.html").read_text(encoding="utf-8")
    m = re.match(r"<!--\s*titel:(.*?)\n\s*beschreibung:(.*?)\s*-->\n", quelle, re.S)
    if not m:
        sys.exit(f"{name}.html: Kopfkommentar fehlt")
    inhalt = ersetze(quelle[m.end():], versionen)
    klasse = "wrap wide" if name == "index" else "wrap"
    seite = kopf(ersetze(m.group(1).strip(), versionen), name, ersetze(m.group(2).strip(), versionen))
    seite += f'<main class="{klasse}">\n{inhalt}\n</main>\n' + fuss(versionen)
    (DIST / f"{name}.html").write_text(seite, encoding="utf-8")


def bilder_einfuegen(body, projekt):
    for anker, bilder in HANDBUCH_BILDER.get(projekt, {}).items():
        m = re.search(rf'<h[23][^>]*\bid="{re.escape(anker)}"[^>]*>.*?</h[23]>', body, re.S)
        if not m:
            sys.exit(f"Handbuch {projekt}: Anker {anker} fehlt")
        rest = body[m.end():]
        ende = re.search(r"</p>|</pre>|</table>|</ul>", rest)
        naechste = re.search(r"<h[23]\b", rest)
        pos = m.end()
        if ende and (not naechste or ende.start() < naechste.start()):
            pos += ende.end()
        figs = "\n".join(bild_html(n, t) for n, t in bilder)
        body = body[:pos] + "\n" + figs + "\n" + body[pos:]
    return body


HANDBUCH_CSS = """
<link rel="stylesheet" href="assets/style.css">
<style>
  body { font-size: 16px; }
  .wrap { max-width: 920px; }
  .handbuch-marke { background: var(--panel); border: 1px solid var(--line); border-left: 4px solid var(--wb-orange); padding: .6rem .9rem; border-radius: 0 4px 4px 0; margin: 1rem 0; }
</style>
"""


def handbuch_seite(projekt, inhalt_kopf, body, versionen):
    name = PROJEKTE[projekt][0]
    v = versionen[projekt]
    marke = (f'<p class="handbuch-marke">Dieses Handbuch gehört zu <strong>{name} {v}</strong>. '
             f'Der schnelle Einstieg steht in der <a href="{projekt}.html">Schritt-für-Schritt-Anleitung</a>.</p>')
    body = re.sub(r"(<h1\b[^>]*>.*?</h1>\s*(?:<p class=\"subtitle\">.*?</p>)?)",
                  lambda m: m.group(1) + "\n" + marke, body, count=1, flags=re.S)
    body = bilder_einfuegen(body, projekt)
    nav = kopf("", projekt, "").split("<body>\n", 1)[1]
    seite = (f'<!doctype html>\n<html lang="de">\n<head>\n<meta charset="utf-8">\n'
             f'<meta name="viewport" content="width=device-width, initial-scale=1">\n'
             f"<title>{name} {v} – Handbuch</title>\n"
             f'<meta name="description" content="Vollständiges Handbuch zu {name} {v} mit Bildern.">\n'
             f"{inhalt_kopf}{HANDBUCH_CSS}</head>\n<body>\n{nav}{body}\n{fuss(versionen)}")
    (DIST / f"{projekt}-handbuch.html").write_text(seite, encoding="utf-8")


def baue_html_handbuch(projekt, pfad, versionen):
    q = (DEV / pfad).read_text(encoding="utf-8")
    if f"Programmversion {versionen[projekt]})" not in q:
        print(f"WARNUNG: {pfad} nennt nicht Programmversion {versionen[projekt]}: Handbuch prüfen.")
    stil = re.search(r"<style>.*?</style>", q, re.S).group(0)
    body = re.search(r"<body[^>]*>(.*)</body>", q, re.S).group(1)
    handbuch_seite(projekt, stil + "\n", body, versionen)


# --- AmigaGuide und Markdown (nur AmiDevIDE hat kein HTML-Handbuch) ---------

def inline_guide(t):
    t = html.escape(t, quote=False)
    t = re.sub(r'@\{"\s*(.*?)\s*" link (\w+)[^}]*\}',
               lambda m: f'<a href="#node-{m.group(2).lower()}">{m.group(1)}</a>', t)
    t = t.replace("@{b}", "<strong>").replace("@{ub}", "</strong>")
    t = t.replace("@{i}", "<em>").replace("@{ui}", "</em>")
    t = t.replace("@{u}", "").replace("@{uu}", "")
    return re.sub(r"@\{[^}]*\}", "", t)


def guide_nach_html(guide):
    aus, inhalt = [], []
    for name, titel, text in re.findall(r'@node\s+(\w+)\s+"(.*?)"\s*\n(.*?)@endnode', guide, re.S | re.I):
        if name.lower() == "main":
            continue
        anker = f"node-{name.lower()}"
        inhalt.append(f'<li><a href="#{anker}">{html.escape(titel)}</a></li>')
        aus.append(f'<h2 id="{anker}">{html.escape(titel)}</h2>')
        text = re.sub(r"^\s*@\{b\}.*?@\{ub\}\s*\n", "", text.strip("\n"), count=1)
        for block in re.split(r"\n\s*\n", text):
            if not block.strip():
                continue
            zeilen = block.split("\n")
            kopf_m = re.fullmatch(r"\s*@\{u\}(.*?)@\{uu\}\s*", zeilen[0])
            if kopf_m:
                aus.append(f"<h3>{inline_guide(kopf_m.group(1))}</h3>")
                zeilen = zeilen[1:]
                if not zeilen:
                    continue
            if all(re.match(r"\s*-\s", z) or z.startswith("    ") for z in zeilen) and re.match(r"\s*-\s", zeilen[0]):
                punkte, akt = [], ""
                for z in zeilen:
                    if re.match(r"\s*-\s", z):
                        if akt:
                            punkte.append(akt)
                        akt = re.sub(r"^\s*-\s", "", z)
                    else:
                        akt += " " + z.strip()
                punkte.append(akt)
                aus.append("<ul>" + "".join(f"<li>{inline_guide(p)}</li>" for p in punkte) + "</ul>")
            elif all(z.startswith("  ") for z in zeilen):
                sauber = "\n".join(z[2:] for z in zeilen)
                aus.append(f"<pre>{inline_guide(sauber)}</pre>")
            else:
                aus.append(f"<p>{inline_guide(' '.join(z.strip() for z in zeilen))}</p>")
    return "\n".join(aus), inhalt


def inline_md(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
    return re.sub(r"\[([^\]]+)\]\((https?://[^)]+)\)", r'<a href="\2">\1</a>', t)


def md_nach_html(md, praefix):
    aus, inhalt = [], []
    zeilen, i = md.split("\n"), 0
    while i < len(zeilen):
        z = zeilen[i]
        if z.startswith("```"):
            j = i + 1
            while j < len(zeilen) and not zeilen[j].startswith("```"):
                j += 1
            aus.append("<pre>" + html.escape("\n".join(zeilen[i + 1:j]), quote=False) + "</pre>")
            i = j + 1
        elif m := re.match(r"(#{1,4})\s+(.*)", z):
            ebene = min(len(m.group(1)) + 1, 4)
            if ebene == 2:  # Dokumenttitel wird zum Kapitel
                pass
            anker = f"{praefix}-{len(aus)}"
            aus.append(f'<h{ebene} id="{anker}">{inline_md(m.group(2))}</h{ebene}>')
            if ebene == 2:
                inhalt.append(f'<li><a href="#{anker}">{inline_md(m.group(2))}</a></li>')
            i += 1
        elif z.startswith("|") and i + 1 < len(zeilen) and re.match(r"\|[\s:|-]+\|?$", zeilen[i + 1]):
            zellen = lambda s: [c.strip() for c in s.strip().strip("|").split("|")]
            kopf_z = "".join(f"<th>{inline_md(c)}</th>" for c in zellen(z))
            i += 2
            reihen = []
            while i < len(zeilen) and zeilen[i].startswith("|"):
                reihen.append("<tr>" + "".join(f"<td>{inline_md(c)}</td>" for c in zellen(zeilen[i])) + "</tr>")
                i += 1
            aus.append(f'<div class="tabelle"><table><tr>{kopf_z}</tr>{"".join(reihen)}</table></div>')
        elif re.match(r"\s*([-*]|\d+\.)\s", z):
            art = "ol" if re.match(r"\s*\d+\.", z) else "ul"
            punkte = []
            while i < len(zeilen) and (re.match(r"\s*([-*]|\d+\.)\s", zeilen[i]) or (zeilen[i].startswith("  ") and zeilen[i].strip())):
                if re.match(r"\s*([-*]|\d+\.)\s", zeilen[i]):
                    punkte.append(re.sub(r"^\s*([-*]|\d+\.)\s+", "", zeilen[i]))
                else:
                    punkte[-1] += " " + zeilen[i].strip()
                i += 1
            aus.append(f"<{art}>" + "".join(f"<li>{inline_md(p)}</li>" for p in punkte) + f"</{art}>")
        elif not z.strip():
            i += 1
        else:
            absatz = []
            while i < len(zeilen) and zeilen[i].strip() and not re.match(r"(#{1,4}\s|```|\||\s*([-*]|\d+\.)\s)", zeilen[i]):
                absatz.append(zeilen[i].strip())
                i += 1
            aus.append(f"<p>{inline_md(' '.join(absatz))}</p>")
    return "\n".join(aus), inhalt


def baue_ide_handbuch(versionen):
    guide = (DEV / "AmiDevIDE/ide/amiga/AmigaIDE.guide").read_bytes().decode("latin-1")
    teil, inhalt = guide_nach_html(guide)
    md = (DEV / "AmiDevIDE/INSTALL.md").read_text(encoding="utf-8")
    inst, inhalt_i = md_nach_html(md, "inst")
    v = versionen["amidevide"]
    body = (f'<main class="wrap"><h1>AmiDevIDE</h1><p class="lead">Handbuch – Entwicklungsumgebung für AmigaOS '
            f'und den PC · Version {v}</p>'
            f'<p class="hinweis"><strong>Hinweis:</strong> Experimentelle Software, entwickelt mit Hilfe von Claude '
            f'(KI-Assistent von Anthropic) und in erster Linie für den Eigengebrauch des Autors geschrieben.</p>'
            f'<nav class="inhalt"><strong>Inhalt</strong><ol><li><a href="#teil-bedienung">Bedienung von AmigaIDE</a>'
            f'<ol>{"".join(inhalt)}</ol></li><li><a href="#teil-installation">Installation</a></li></ol></nav>'
            f'<h2 id="teil-bedienung">Bedienung von AmigaIDE</h2>{teil}'
            f'<h2 id="teil-installation">Installation</h2>'
            f'<p class="hinweis">Dieser Teil beschreibt den Aufbau der kompletten Entwicklungsumgebung aus den '
            f'Quellen. Die Quellen werden später auf GitHub veröffentlicht. Das Amiga-Programm gibt es schon '
            f'<a href="{DOWNLOADS["amidevide"]}">auf Aminet</a>.</p>{inst}</main>')
    handbuch_seite("amidevide", "", body, versionen)


def main():
    versionen = lies_versionen()
    for k, v in BILDER_VERSION.items():
        if versionen[k] != v:
            print(f"WARNUNG: Bilder von {PROJEKTE[k][0]} stammen aus {v}, aktuell ist {versionen[k]}: "
                  "Bilder prüfen und BILDER_VERSION nachziehen.")
    if DIST.exists():
        shutil.rmtree(DIST)
    (DIST / "assets").mkdir(parents=True)
    (DIST / "img").mkdir()
    (DIST / "data").mkdir()
    shutil.copy(SRC / "assets/style.css", DIST / "assets/style.css")
    shutil.copy(SRC / "counter.php", DIST / "counter.php")
    shutil.copy(SRC / ".htaccess", DIST / ".htaccess")
    shutil.copy(SRC / "data/.htaccess", DIST / "data/.htaccess")

    for name in ["index", "datenschutz", *PROJEKTE]:
        baue_seite(name, versionen)
    baue_html_handbuch("amicloud", "AmiCloud/docs/user/AmiCloud-Handbuch.html", versionen)
    baue_html_handbuch("amivcs", "AmiVCS/docs/user/AmiVCS-Handbuch.html", versionen)
    baue_html_handbuch("amiowncloud", "AmiOwnCloud/docs/user/AmiOwnCloud-Handbuch.html", versionen)
    baue_ide_handbuch(versionen)

    if NOINDEX:
        for seite in DIST.glob("*.html"):
            text = seite.read_text(encoding="utf-8")
            if "</head>" not in text:
                sys.exit(f"{seite.name}: kein </head>")
            seite.write_text(text.replace("</head>", '<meta name="robots" content="noindex, nofollow">\n</head>', 1), encoding="utf-8")
        with open(DIST / ".htaccess", "a", encoding="utf-8") as f:
            f.write('<IfModule mod_headers.c>\n  Header set X-Robots-Tag "noindex, nofollow"\n</IfModule>\n')

    # Bilder: Amiga-Aufnahmen bleiben verlustfreie PNGs (klein und pixelgenau),
    # Mac und Browser werden WebP.
    benutzt = set(re.findall(r'img/([\w-]+\.(?:png|webp))', "".join(p.read_text(encoding="utf-8") for p in DIST.glob("*.html"))))
    for datei in sorted(benutzt):
        stamm, endung = datei.rsplit(".", 1)
        q = IMG / f"{stamm}.png"
        if not q.exists():
            fehlende_bilder.add(stamm)
        elif endung == "png":
            shutil.copy(q, DIST / "img" / datei)
        else:
            subprocess.run(["cwebp", "-quiet", "-q", "82", "-m", "6", "-mt", str(q), "-o", str(DIST / "img" / datei)], check=True)

    unbenutzt = {p.stem for p in IMG.glob("*.png")} - {b.rsplit(".", 1)[0] for b in benutzt}
    if unbenutzt:
        print("Nicht verwendete Bilder:", ", ".join(sorted(unbenutzt)))
    if fehlende_bilder:
        print("FEHLENDE BILDER:", ", ".join(sorted(fehlende_bilder)))
        sys.exit(1)
    groesse = sum(p.stat().st_size for p in DIST.rglob("*") if p.is_file())
    print("Fertig:", DIST, f"({groesse / 1048576:.1f} MB),", ", ".join(f"{PROJEKTE[k][0]} {v}" for k, v in versionen.items()))


if __name__ == "__main__":
    main()
