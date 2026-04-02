# MiniPHPServer v2.0

A lightweight, portable HTTP server for Windows written in pure C (Win32 + WinSock2).  
No dependencies. No installer. Just drop it in a folder and run.

---

## Requirements

| Requirement | Notes |
|---|---|
| Windows 7 or newer | 64-bit recommended |
| `php\php-cgi.exe` | Only needed if you use PHP files |

No external libraries. No .NET. No Python. Nothing.

---

## Folder Structure

```
MiniPHPServer/
│
├── MiniPHPServer.exe       ← the server
├── host.json               ← configuration (port)
├── access.log              ← request log (auto-created)
│
├── php/                    ← PHP runtime (optional)
│   ├── php-cgi.exe         ← required for PHP support
│   ├── php.ini             ← PHP configuration
│   └── ...                 ← other PHP files
│
└── www/                    ← web root (your website goes here)
    ├── index.php           ← or index.html / index.htm
    ├── style.css
    ├── script.js
    └── ...
```

> The server always looks for `index.php` first, then `index.html`, then `index.htm`.

---

## Quick Start

**1. Build** (skip if you already have the `.exe`):
```bat
build.bat
```

**2. Create your web root:**
```
mkdir www
echo Hello World > www\index.html
```

**3. Run:**
```bat
MiniPHPServer.exe
```

**4. Open your browser:**
```
http://localhost:80
```

---

## Configuration

Edit `host.json` to change the port:

```json
{"port": 80}
```

| Port | Notes |
|---|---|
| `80` | Standard HTTP (may require admin rights) |
| `8080` | Default fallback if host.json is missing |
| `3000` | Any free port works |

---

## PHP Support

To enable PHP, place a PHP runtime next to the server:

```
MiniPHPServer/
└── php/
    ├── php-cgi.exe    ← download from https://windows.php.net/download
    └── php.ini
```

### Recommended php.ini settings

```ini
cgi.force_redirect = 0
cgi.fix_pathinfo = 1
session.save_path = "C:/path/to/sessions"
upload_tmp_dir = "C:/path/to/tmp"
extension_dir = "ext"
```

### Supported PHP features

| Feature | Status |
|---|---|
| `$_GET` | ✅ |
| `$_POST` | ✅ |
| `$_COOKIE` | ✅ |
| `$_SESSION` | ✅ |
| `$_SERVER` | ✅ |
| `header()` redirects | ✅ |
| `setcookie()` | ✅ |
| File uploads | ✅ |
| Custom HTTP status codes | ✅ |

---

## Supported HTTP Methods

| Method | Support |
|---|---|
| `GET` | ✅ |
| `POST` | ✅ |
| `HEAD` | ✅ (headers only, no body) |
| `PUT` / `DELETE` / `PATCH` | ⚠️ Passed to PHP, PHP must handle them |

---

## Supported File Types

| Extension | MIME Type |
|---|---|
| `.html`, `.htm` | `text/html` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.json` | `application/json` |
| `.xml` | `application/xml` |
| `.svg` | `image/svg+xml` |
| `.png`, `.jpg`, `.gif`, `.webp`, `.ico` | `image/*` |
| `.woff`, `.woff2`, `.ttf` | `font/*` |
| `.pdf` | `application/pdf` |
| `.zip` | `application/zip` |
| anything else | `application/octet-stream` |

---

## Access Log

All requests are logged to `access.log`:

```
2025-04-02 14:22:01 | 200   | GET    | /index.php                    | 127.0.0.1
2025-04-02 14:22:01 | 200   | GET    | /style.css                    | 127.0.0.1
2025-04-02 14:22:05 | 404   | GET    | /missing.html                 | 127.0.0.1
```

---

## Security Notes

- Path traversal (`../`) is blocked — returns `403 Forbidden`
- The server binds to `0.0.0.0` — it is accessible from your local network
- **Do not expose this server to the public internet** — it is intended for local development only

---

## Building from Source

Requirements: GCC (MinGW-w64)

```bat
gcc main2_0.c -o MiniPHPServer.exe -lws2_32 -O2
```

Or use the included `build.bat` which also copies the required runtime DLLs next to the executable.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| `Bind failed` | Port is already in use — change port in `host.json` or close the other app |
| PHP page shows blank | Check that `php\php-cgi.exe` exists and `php.ini` has `cgi.force_redirect=0` |
| Port 80 access denied | Run `MiniPHPServer.exe` as Administrator |
| Chinese / Czech characters broken | Make sure your PHP files are saved as UTF-8 |

---

## License

Do whatever you want with it.
