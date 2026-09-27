<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

class Auth
{
    public static int    $user_id  = 0;
    public static string $username = '';

    public static function requireBasicAuth(): void
    {
        $user = $_SERVER['PHP_AUTH_USER'] ?? '';
        $pass = $_SERVER['PHP_AUTH_PW']   ?? '';

        if ($user === '' || $pass === '') {
            header('WWW-Authenticate: Basic realm="AmiCloud"');
            Response::error(401, 'Authentication required');
        }

        $stmt = DB::get()->prepare(
            'SELECT id, pass_hash FROM users WHERE username = ? AND active = 1 LIMIT 1'
        );
        $stmt->execute([$user]);
        $row = $stmt->fetch();

        if (!$row || !password_verify($pass, $row['pass_hash'])) {
            header('WWW-Authenticate: Basic realm="AmiCloud"');
            Response::error(401, 'Invalid credentials');
        }

        self::$user_id  = (int)$row['id'];
        self::$username = $user;
    }
}
