<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

class Response
{
    public static function json(array $data, int $code = 200): void
    {
        http_response_code($code);
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    public static function ok(array $extra = []): void
    {
        self::json(array_merge(['status' => 'ok'], $extra));
    }

    public static function error(int $code, string $message): void
    {
        self::json(['status' => 'error', 'code' => $code, 'message' => $message], $code);
    }

    public static function raw(string $data, string $mime = 'application/octet-stream'): void
    {
        header('Content-Type: ' . $mime);
        header('Content-Length: ' . strlen($data));
        echo $data;
        exit;
    }

    /* $partial: Antwort auf einen Range-Header -- 206 mit Content-Range
     * statt 200 (offset/length per Query-Parameter bleiben 200). */
    public static function stream(string $filepath, string $mime, int $offset, int $length,
                                  bool $partial = false): void
    {
        $size = filesize($filepath);
        $end  = ($length >= 0) ? min($offset + $length, $size) : $size;
        $send = max(0, $end - $offset);

        if ($partial) {
            http_response_code(206);
            header('Content-Range: bytes ' . $offset . '-' . ($offset + $send - 1) . '/' . $size);
        }
        header('Accept-Ranges: bytes');
        header('Content-Type: ' . $mime);
        header('Content-Length: ' . $send);
        /* Dieselbe Zahl noch einmal: hinter Apache + PHP-FPM kommt die
         * Antwort chunked an, Content-Length geht dabei verloren. Die
         * Clients brauchen die Laenge fuer die Fortschrittsanzeige. */
        header('X-AmiCloud-Length: ' . $send);

        $fp = fopen($filepath, 'rb');
        if ($offset > 0) {
            fseek($fp, $offset);
        }
        $remaining = $send;
        while ($remaining > 0 && !feof($fp)) {
            $chunk = fread($fp, min(8192, $remaining));
            echo $chunk;
            $remaining -= strlen($chunk);
        }
        fclose($fp);
        exit;
    }
}
