<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Zentrale App-Version — hier aendern, NICHT in config.php.
 * config.php ist eine pro-Deployment editierte Settings-Datei; fehlte
 * dort AMICLOUD_VERSION, fiel PHP 7.4 auf den woertlichen Bareword-String
 * zurueck ("version":"AMICLOUD_VERSION" in der status-Antwort statt der
 * echten Nummer). Der Guard unten erlaubt trotzdem Uebergangssicherheit,
 * falls ein noch nicht aktualisiertes config.php die Konstante selbst
 * definiert. */
if (!defined('AMICLOUD_VERSION')) {
    define('AMICLOUD_VERSION', '0.1');
}
