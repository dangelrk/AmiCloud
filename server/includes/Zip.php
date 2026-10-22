<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Mehrere Dateien/Ordner des aktuellen Benutzers (Auth) als ZIP
 * ausliefern -- Web-Auswahl "Herunterladen" und Ordner-Links in
 * share.php. Die Temp-Datei wird auch bei Abbruch aufgeraeumt. */
class Zip
{
    const MAX_BYTES = 1073741824; // 1 GB
    const MAX_FILES = 20000;
    const SLOTS     = 2;          // gleichzeitige ZIP-Erstellungen je Server

    /* Begrenzt parallele ZIP-Erstellungen (CPU, Platte in /tmp): einer
     * von SLOTS Sperrdateien muss frei sein. Der Lock faellt mit dem
     * Request-Ende automatisch weg. */
    private static $slot = null;

    private static function takeSlot(): bool
    {
        for ($i = 0; $i < self::SLOTS; $i++) {
            $fp = @fopen(sys_get_temp_dir() . '/amicloud-zip-' . $i . '.lock', 'c');
            if ($fp && flock($fp, LOCK_EX | LOCK_NB)) {
                self::$slot = $fp;
                return true;
            }
            if ($fp) {
                fclose($fp);
            }
        }
        return false;
    }

    /* $paths: benutzerrelative Pfade ("/a/b"). Liefert nur bei Fehler
     * zurueck (Meldung), sonst endet der Request mit dem Download. */
    public static function send(array $paths, string $zipName): string
    {
        if (!class_exists('ZipArchive')) {
            return 'ZIP-Download ist auf diesem Server nicht verfügbar (PHP-Erweiterung zip fehlt).';
        }
        $files = [];
        $total = 0;
        foreach ($paths as $p) {
            $abs = storagePath($p);
            if (!file_exists($abs)) {
                continue;
            }
            $base = basename($abs);
            if (is_file($abs)) {
                $files[] = [$abs, $base];
                $total  += (int)filesize($abs);
                continue;
            }
            $files[] = [null, $base . '/'];
            $it = new RecursiveIteratorIterator(
                new RecursiveDirectoryIterator($abs, FilesystemIterator::SKIP_DOTS),
                RecursiveIteratorIterator::SELF_FIRST
            );
            foreach ($it as $f) {
                $rel = $base . '/' . substr($f->getPathname(), strlen($abs) + 1);
                if ($f->isLink()) {
                    continue;
                }
                if ($f->isDir()) {
                    $files[] = [null, $rel . '/'];
                } else {
                    $files[] = [$f->getPathname(), $rel];
                    $total  += $f->getSize();
                }
                if ($total > self::MAX_BYTES || count($files) > self::MAX_FILES) {
                    break;
                }
            }
            if ($total > self::MAX_BYTES || count($files) > self::MAX_FILES) {
                break;
            }
        }
        if (count($files) > self::MAX_FILES) {
            return 'Die Auswahl enthält mehr als ' . self::MAX_FILES . ' Dateien – bitte kleinere Ordner wählen.';
        }
        if ($files === []) {
            return 'Nichts zum Herunterladen ausgewählt.';
        }
        if ($total > self::MAX_BYTES) {
            return 'Die Auswahl ist größer als 1 GB – bitte einzeln herunterladen.';
        }

        if (!self::takeSlot()) {
            http_response_code(429);
            return 'Gerade werden zu viele ZIP-Dateien erstellt. Bitte gleich noch einmal versuchen.';
        }
        $tmp = tempnam(sys_get_temp_dir(), 'amizip');
        register_shutdown_function(function () use ($tmp) {
            @unlink($tmp);
        });
        $zip = new ZipArchive();
        if ($zip->open($tmp, ZipArchive::OVERWRITE) !== true) {
            return 'ZIP-Datei konnte nicht erstellt werden.';
        }
        foreach ($files as [$abs, $name]) {
            if ($abs === null) {
                $zip->addEmptyDir(rtrim($name, '/'));
            } else {
                $zip->addFile($abs, $name);
                /* Ohne Kompression: spart CPU, Amiga-Archive sind meist
                 * ohnehin gepackt */
                $zip->setCompressionName($name, ZipArchive::CM_STORE);
            }
        }
        $zip->close();
        @set_time_limit(0);
        FileStore::sendAttachment($tmp, $zipName);
        return '';
    }
}
