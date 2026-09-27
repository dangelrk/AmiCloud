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

    public static function stream(string $filepath, string $mime, int $offset, int $length): void
    {
        $size = filesize($filepath);
        $end  = ($length >= 0) ? min($offset + $length, $size) : $size;
        $send = $end - $offset;

        header('Content-Type: ' . $mime);
        header('Content-Length: ' . $send);

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
