<?php
/* Besucherzaehler der AmiCloud-Anleitung.
 *
 * Wird von jeder Seite als Bild eingebunden und zaehlt dabei einen Seitenaufruf.
 * Speichert nur eine einzige Zahl: keine IP-Adressen, keine Cookies, kein
 * Dienst von Dritten. Laeuft ab PHP 7.4.
 *
 * Die Zahl liegt in data/counter.txt. Der Webserver muss dort schreiben duerfen
 * (siehe README). Kann er das nicht, zeigt das Bild einen Strich statt einer
 * Zahl, die Seite selbst bleibt davon unberuehrt. */

function amiCounterHit(string $file): ?int
{
    $fp = @fopen($file, 'c+');
    if ($fp === false) {
        return null;
    }
    $count = null;
    if (flock($fp, LOCK_EX)) {
        $count = (int) trim((string) stream_get_contents($fp)) + 1;
        rewind($fp);
        ftruncate($fp, 0);
        fwrite($fp, (string) $count);
        fflush($fp);
        flock($fp, LOCK_UN);
    }
    fclose($fp);
    return $count;
}

$count = amiCounterHit(__DIR__ . '/data/counter.txt');
$text = $count === null ? '------' : str_pad((string) $count, 6, '0', STR_PAD_LEFT);

$cellW = 15;
$cellH = 22;
$width = strlen($text) * $cellW;
$cells = '';
foreach (str_split($text) as $i => $digit) {
    $x = $i * $cellW;
    $cells .= '<rect x="' . ($x + 1) . '" y="1" width="' . ($cellW - 2) . '" height="' . ($cellH - 2)
        . '" rx="2" fill="#10203a"/>'
        . '<text x="' . ($x + $cellW / 2) . '" y="16.5" text-anchor="middle" fill="#ffb000">'
        . htmlspecialchars($digit, ENT_QUOTES, 'UTF-8') . '</text>';
}

header('Content-Type: image/svg+xml; charset=utf-8');
header('Cache-Control: no-store, max-age=0');
header('X-Content-Type-Options: nosniff');
echo '<svg xmlns="http://www.w3.org/2000/svg" width="' . $width . '" height="' . $cellH
    . '" viewBox="0 0 ' . $width . ' ' . $cellH . '" role="img" aria-label="' . $text . ' Seitenaufrufe">'
    . '<g font-family="Menlo,Consolas,monospace" font-size="15" font-weight="700">' . $cells . '</g></svg>';
