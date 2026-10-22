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
                if ($f->isDir()) {
                    $files[] = [null, $rel . '/'];
                } else {
                    $files[] = [$f->getPathname(), $rel];
                    $total  += $f->getSize();
                }
            }
            if ($total > self::MAX_BYTES) {
                break;
            }
        }
        if ($files === []) {
            return 'Nichts zum Herunterladen ausgewählt.';
        }
        if ($total > self::MAX_BYTES) {
            return 'Die Auswahl ist größer als 1 GB – bitte einzeln herunterladen.';
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
            }
        }
        $zip->close();
        @set_time_limit(0);
        FileStore::sendAttachment($tmp, $zipName);
        return '';
    }
}
