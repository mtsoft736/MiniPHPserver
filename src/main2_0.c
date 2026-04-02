#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>

#pragma comment(lib,"ws2_32.lib")

#define VERSION     "2.0"
#define MAXBUF      16384
#define PHP_BUF     (1024*1024)   // 1 MB pro PHP output
#define WWW_DIR     "www"
#define PHP_CGI     "php\\php-cgi.exe"
#define CONFIG_FILE "host.json"
#define LOG_FILE    "access.log"

// ─────────────────────────────────────────────────────────────────────────────
// Struktury
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    SOCKET client;
    struct sockaddr_in addr;
} ClientInfo;

typedef struct {
    char method[16];
    char path[512];
    char query[1024];
    char version[16];
    char content_type[128];
    char content_length_str[32];
    int  content_length;
    char host[256];
    char cookie[1024];
    char *body;
    int   body_len;
} HttpRequest;

// ─────────────────────────────────────────────────────────────────────────────
// Konfigurace
// ─────────────────────────────────────────────────────────────────────────────
int load_port(void) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return 8080;
    char buf[256]; int port = 8080;
    fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    sscanf(buf, "{\"port\":%d}", &port);
    return port;
}

// ─────────────────────────────────────────────────────────────────────────────
// Logging
// ─────────────────────────────────────────────────────────────────────────────
CRITICAL_SECTION g_log_cs;

void log_request(const char *ip, const char *method, const char *path, int code) {
    EnterCriticalSection(&g_log_cs);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d | %-5d | %-6s | %-30s | %s\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond,
                code, method, path, ip);
        fclose(f);
    }
    LeaveCriticalSection(&g_log_cs);
    printf("  [%d] %s %s  (%s)\n", code, method, path, ip);
}

// ─────────────────────────────────────────────────────────────────────────────
// Soubory / MIME
// ─────────────────────────────────────────────────────────────────────────────
int file_exists(const char *path) {
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR) == 0;
}

int dir_exists(const char *path) {
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
}

int is_php(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && _stricmp(ext, ".php") == 0;
}

const char *get_mime(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (_stricmp(ext, ".html") == 0 || _stricmp(ext, ".htm") == 0) return "text/html";
    if (_stricmp(ext, ".css")  == 0) return "text/css";
    if (_stricmp(ext, ".js")   == 0) return "application/javascript";
    if (_stricmp(ext, ".json") == 0) return "application/json";
    if (_stricmp(ext, ".xml")  == 0) return "application/xml";
    if (_stricmp(ext, ".txt")  == 0) return "text/plain";
    if (_stricmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (_stricmp(ext, ".png")  == 0) return "image/png";
    if (_stricmp(ext, ".jpg")  == 0 || _stricmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(ext, ".gif")  == 0) return "image/gif";
    if (_stricmp(ext, ".ico")  == 0) return "image/x-icon";
    if (_stricmp(ext, ".webp") == 0) return "image/webp";
    if (_stricmp(ext, ".woff") == 0) return "font/woff";
    if (_stricmp(ext, ".woff2")== 0) return "font/woff2";
    if (_stricmp(ext, ".ttf")  == 0) return "font/ttf";
    if (_stricmp(ext, ".pdf")  == 0) return "application/pdf";
    if (_stricmp(ext, ".zip")  == 0) return "application/zip";
    return "application/octet-stream";
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP helpers
// ─────────────────────────────────────────────────────────────────────────────
void send_all(SOCKET s, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
}

void send_error(SOCKET cs, int code, const char *title, const char *detail) {
    char html[2048];
    int hlen = snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>%d %s</title><style>"
        "body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;"
        "align-items:center;justify-content:center;height:100vh;flex-direction:column}"
        "h1{font-size:80px;margin:0;color:#e94560}h2{margin:10px 0;color:#aaa}"
        "p{color:#888;max-width:400px;text-align:center}"
        "</style></head><body>"
        "<h1>%d</h1><h2>%s</h2><p>%s</p></body></html>",
        code, title, code, title, detail);

    char header[512];
    int hl = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        code, title, hlen);

    send_all(cs, header, hl);
    send_all(cs, html, hlen);
}

// ─────────────────────────────────────────────────────────────────────────────
// URL decode
// ─────────────────────────────────────────────────────────────────────────────
void url_decode(const char *src, char *dst, int dmax) {
    int i = 0;
    while (*src && i < dmax - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' '; src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parsování HTTP requestu
// ─────────────────────────────────────────────────────────────────────────────
// Pomocník: najde hlavičku (case-insensitive)
static const char *find_header(const char *req, const char *name) {
    char needle[128];
    snprintf(needle, sizeof(needle), "%s:", name);
    // hledáme za první \n
    const char *p = req;
    while ((p = strchr(p, '\n')) != NULL) {
        p++;
        if (_strnicmp(p, needle, strlen(needle)) == 0) {
            p += strlen(needle);
            while (*p == ' ') p++;
            return p;
        }
    }
    return NULL;
}

static void copy_header_value(const char *src, char *dst, int dmax) {
    if (!src) { dst[0] = 0; return; }
    int i = 0;
    while (src[i] && src[i] != '\r' && src[i] != '\n' && i < dmax - 1)
        dst[i] = src[i], i++;
    dst[i] = 0;
}

int parse_request(const char *raw, int raw_len, HttpRequest *r) {
    memset(r, 0, sizeof(*r));

    // Request line
    char raw_path[512];
    if (sscanf(raw, "%15s %511s %15s", r->method, raw_path, r->version) != 3)
        return 0;

    // Oddělit query string
    char *q = strchr(raw_path, '?');
    if (q) {
        *q = 0;
        strncpy(r->query, q + 1, sizeof(r->query) - 1);
    }
    url_decode(raw_path, r->path, sizeof(r->path));

    // Hlavičky
    const char *hdr;

    hdr = find_header(raw, "Content-Type");
    copy_header_value(hdr, r->content_type, sizeof(r->content_type));

    hdr = find_header(raw, "Content-Length");
    copy_header_value(hdr, r->content_length_str, sizeof(r->content_length_str));
    if (r->content_length_str[0])
        r->content_length = atoi(r->content_length_str);

    hdr = find_header(raw, "Host");
    copy_header_value(hdr, r->host, sizeof(r->host));

    hdr = find_header(raw, "Cookie");
    copy_header_value(hdr, r->cookie, sizeof(r->cookie));

    // Body
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        int body_in_buf = raw_len - (int)(body_start - raw);
        if (body_in_buf > 0 && r->content_length > 0) {
            int blen = body_in_buf < r->content_length ? body_in_buf : r->content_length;
            r->body = (char*)malloc(blen + 1);
            if (r->body) {
                memcpy(r->body, body_start, blen);
                r->body[blen] = 0;
                r->body_len = blen;
            }
        }
    }

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// PHP CGI runner – v2: správné env, správné hlavičky, cookies, sessions
// ─────────────────────────────────────────────────────────────────────────────
char *run_php(const char *script, const HttpRequest *r, const char *ip, int *out_len) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE out_r, out_w, in_r, in_w;

    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return NULL;
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return NULL;
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);

    // Absolutní cesta ke skriptu (php-cgi to vyžaduje)
    char abs_script[MAX_PATH];
    GetFullPathNameA(script, MAX_PATH, abs_script, NULL);

    // Absolutní cesta k www root
    char abs_www[MAX_PATH];
    GetFullPathNameA(WWW_DIR, MAX_PATH, abs_www, NULL);

    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "%s -q", PHP_CGI);

    // Sestavení environment bloku (null-separated, double-null terminated)
    // Použijeme SetEnvironmentVariable – jednodušší a bezpečnější než env blok
    SetEnvironmentVariableA("REDIRECT_STATUS",   "200");
    SetEnvironmentVariableA("GATEWAY_INTERFACE", "CGI/1.1");
    SetEnvironmentVariableA("SERVER_PROTOCOL",   r->version[0] ? r->version : "HTTP/1.1");
    SetEnvironmentVariableA("SERVER_SOFTWARE",   "MiniPHPServer/" VERSION);
    SetEnvironmentVariableA("SERVER_NAME",        r->host[0] ? r->host : "localhost");
    SetEnvironmentVariableA("SERVER_PORT",        "80");
    SetEnvironmentVariableA("REQUEST_METHOD",     r->method);
    SetEnvironmentVariableA("REQUEST_URI",        r->path);  // celá URI
    SetEnvironmentVariableA("SCRIPT_FILENAME",    abs_script);
    SetEnvironmentVariableA("SCRIPT_NAME",        r->path);
    SetEnvironmentVariableA("DOCUMENT_ROOT",      abs_www);
    SetEnvironmentVariableA("QUERY_STRING",       r->query);
    SetEnvironmentVariableA("REMOTE_ADDR",        ip);
    SetEnvironmentVariableA("REMOTE_HOST",        ip);
    SetEnvironmentVariableA("CONTENT_TYPE",       r->content_type);
    SetEnvironmentVariableA("CONTENT_LENGTH",     r->content_length_str);
    SetEnvironmentVariableA("HTTP_COOKIE",        r->cookie);
    SetEnvironmentVariableA("HTTP_HOST",          r->host);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = out_w;
    si.hStdError  = out_w;
    si.hStdInput  = in_r;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(out_w); CloseHandle(out_r);
        CloseHandle(in_w);  CloseHandle(in_r);
        return NULL;
    }
    CloseHandle(out_w);
    CloseHandle(in_r);

    // Zapsat POST body
    if (r->body && r->body_len > 0) {
        DWORD written;
        WriteFile(in_w, r->body, r->body_len, &written, NULL);
    }
    CloseHandle(in_w);

    // Číst výstup
    char *buf = (char*)malloc(PHP_BUF);
    if (!buf) {
        CloseHandle(out_r);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        return NULL;
    }

    size_t len = 0; DWORD n;
    while (ReadFile(out_r, buf + len, (DWORD)(PHP_BUF - len - 1), &n, NULL) && n > 0)
        len += n;
    CloseHandle(out_r);

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    buf[len] = 0;
    if (out_len) *out_len = (int)len;
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sestavení odpovědi z PHP CGI výstupu
// Php-cgi vrací vlastní hlavičky, musíme je přeposlat včetně Set-Cookie atd.
// ─────────────────────────────────────────────────────────────────────────────
void send_php_response(SOCKET cs, char *cgi_out, int cgi_len) {
    // Najdeme konec hlaviček
    char *sep = strstr(cgi_out, "\r\n\r\n");
    char *body;
    int body_len;

    if (sep) {
        body = sep + 4;
        body_len = cgi_len - (int)(body - cgi_out);
    } else {
        // Zkus \n\n
        sep = strstr(cgi_out, "\n\n");
        if (sep) {
            body = sep + 2;
            body_len = cgi_len - (int)(body - cgi_out);
        } else {
            body = cgi_out;
            body_len = cgi_len;
            sep = NULL;
        }
    }

    // Přečteme PHP hlavičky a zjistíme status
    int status_code = 200;
    const char *status_text = "OK";
    char php_headers[4096] = {0};
    int php_hlen = 0;

    if (sep) {
        char hdrbuf[4096];
        int hbuflen = (int)(sep - cgi_out);
        if (hbuflen >= (int)sizeof(hdrbuf)) hbuflen = (int)sizeof(hdrbuf) - 1;
        memcpy(hdrbuf, cgi_out, hbuflen);
        hdrbuf[hbuflen] = 0;

        // Projdeme hlavičky řádek po řádku
        char *line = strtok(hdrbuf, "\n");
        while (line) {
            // Odstraň \r na konci
            int ll = (int)strlen(line);
            if (ll > 0 && line[ll-1] == '\r') line[ll-1] = 0;

            if (_strnicmp(line, "Status:", 7) == 0) {
                // PHP vrátil "Status: 302 Found"
                sscanf(line + 7, "%d", &status_code);
                const char *sp = strchr(line + 7, ' ');
                if (sp) { sp++; while(*sp==' ') sp++; status_text = sp; }
            } else if (strlen(line) > 0) {
                // Přidej hlavičku do bufferu (Content-Type, Set-Cookie, Location, ...)
                php_hlen += snprintf(php_headers + php_hlen,
                    sizeof(php_headers) - php_hlen, "%s\r\n", line);
            }
            line = strtok(NULL, "\n");
        }
    }

    // Odešli HTTP odpověď
    char resp_header[512];
    int rhl = snprintf(resp_header, sizeof(resp_header),
        "HTTP/1.1 %d %s\r\n"
        "%s"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text,
        php_headers,
        body_len);

    send_all(cs, resp_header, rhl);
    if (body_len > 0)
        send_all(cs, body, body_len);
}

// ─────────────────────────────────────────────────────────────────────────────
// Obsluha jednoho klienta
// ─────────────────────────────────────────────────────────────────────────────
DWORD WINAPI handle_client(LPVOID arg) {
    ClientInfo *ci = (ClientInfo*)arg;
    SOCKET cs = ci->client;
    char ip[32];
    strcpy(ip, inet_ntoa(ci->addr.sin_addr));
    free(ci);

    // Přijmi request (může být větší u POST)
    char *raw = (char*)malloc(MAXBUF);
    if (!raw) { closesocket(cs); return 0; }
    int total = 0, n;
    while (total < MAXBUF - 1) {
        n = recv(cs, raw + total, MAXBUF - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        // Máme celé hlavičky?
        if (strstr(raw, "\r\n\r\n")) break;
    }
    if (total <= 0) { free(raw); closesocket(cs); return 0; }
    raw[total] = 0;

    HttpRequest req;
    if (!parse_request(raw, total, &req)) {
        free(raw);
        send_error(cs, 400, "Bad Request", "Could not parse HTTP request.");
        closesocket(cs);
        return 0;
    }
    free(raw);

    // Bezpečnost – zakázat path traversal
    if (strstr(req.path, "..") || req.path[0] != '/') {
        if (req.body) free(req.body);
        log_request(ip, req.method, req.path, 403);
        send_error(cs, 403, "Forbidden", "Access to this resource is denied.");
        closesocket(cs); return 0;
    }

    // Přeložit cestu na soubor
    char full[MAX_PATH];
    if (strcmp(req.path, "/") == 0) {
        snprintf(full, sizeof(full), "%s\\index.php",  WWW_DIR); if (file_exists(full)) goto resolved;
        snprintf(full, sizeof(full), "%s\\index.html", WWW_DIR); if (file_exists(full)) goto resolved;
        snprintf(full, sizeof(full), "%s\\index.htm",  WWW_DIR); if (file_exists(full)) goto resolved;
        // žádný index
        if (req.body) free(req.body);
        log_request(ip, req.method, req.path, 404);
        send_error(cs, 404, "Not Found", "No index file found.");
        closesocket(cs); return 0;
    } else {
        // Normalizovat lomítka
        char norm[512];
        strncpy(norm, req.path, sizeof(norm)-1);
        for (char *p = norm; *p; p++) if (*p == '/') *p = '\\';
        snprintf(full, sizeof(full), "%s%s", WWW_DIR, norm);

        // Adresář → hledej index
        if (dir_exists(full)) {
            char tmp[MAX_PATH];
            snprintf(tmp, sizeof(tmp), "%s\\index.php",  full); if (file_exists(tmp)) { strcpy(full, tmp); goto resolved; }
            snprintf(tmp, sizeof(tmp), "%s\\index.html", full); if (file_exists(tmp)) { strcpy(full, tmp); goto resolved; }
            snprintf(tmp, sizeof(tmp), "%s\\index.htm",  full); if (file_exists(tmp)) { strcpy(full, tmp); goto resolved; }
        }
    }

resolved:
    if (!file_exists(full)) {
        if (req.body) free(req.body);
        log_request(ip, req.method, req.path, 404);
        send_error(cs, 404, "Not Found", "The requested resource was not found on this server.");
        closesocket(cs); return 0;
    }

    // HEAD → stejné hlavičky, bez body
    int send_body = (_stricmp(req.method, "HEAD") != 0);

    if (is_php(full)) {
        // ── PHP ──
        int cgi_len = 0;
        char *cgi_out = run_php(full, &req, ip, &cgi_len);
        if (req.body) free(req.body);

        if (!cgi_out) {
            log_request(ip, req.method, req.path, 500);
            send_error(cs, 500, "Internal Server Error", "PHP execution failed. Is php-cgi.exe present?");
            closesocket(cs); return 0;
        }

        log_request(ip, req.method, req.path, 200);
        send_php_response(cs, cgi_out, cgi_len);
        free(cgi_out);

    } else {
        // ── Statický soubor ──
        if (req.body) free(req.body);
        const char *mime = get_mime(full);

        FILE *f = fopen(full, "rb");
        if (!f) {
            log_request(ip, req.method, req.path, 500);
            send_error(cs, 500, "Internal Server Error", "Cannot open file.");
            closesocket(cs); return 0;
        }
        fseek(f, 0, SEEK_END);
        long filesize = ftell(f);
        fseek(f, 0, SEEK_SET);

        int is_text = (strncmp(mime, "text/", 5) == 0 || strstr(mime, "json") || strstr(mime, "xml") || strstr(mime, "javascript"));

        char header[512];
        int hl = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s%s\r\n"
            "Content-Length: %ld\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n\r\n",
            mime, is_text ? "; charset=UTF-8" : "",
            filesize);

        send_all(cs, header, hl);
        log_request(ip, req.method, req.path, 200);

        if (send_body) {
            char fbuf[8192]; size_t rd;
            while ((rd = fread(fbuf, 1, sizeof(fbuf), f)) > 0)
                send_all(cs, fbuf, (int)rd);
        }
        fclose(f);
    }

    closesocket(cs);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
int main(void) {
    InitializeCriticalSection(&g_log_cs);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("[CHYBA] WSAStartup selhal\n"); return 1;
    }

    int port = load_port();

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { printf("[CHYBA] socket()\n"); return 1; }

    // Reuse address
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("[CHYBA] bind() selhal - port %d je obsazen?\n", port); return 1;
    }
    if (listen(srv, 64) < 0) { printf("[CHYBA] listen()\n"); return 1; }

    printf("\n");
    printf("  +-----------------------------------------+\n");
    printf("  |   MiniPHPServer v%-6s                  |\n", VERSION);
    printf("  +-----------------------------------------+\n");
    printf("  |  http://localhost:%-5d                  |\n", port);
    printf("  |  www root : %-28s|\n", WWW_DIR);
    printf("  |  php-cgi  : %-28s|\n", PHP_CGI);
    printf("  +-----------------------------------------+\n\n");

    while (1) {
        struct sockaddr_in client; int clen = sizeof(client);
        SOCKET cs = accept(srv, (struct sockaddr*)&client, &clen);
        if (cs == INVALID_SOCKET) continue;

        ClientInfo *ci = (ClientInfo*)malloc(sizeof(ClientInfo));
        if (!ci) { closesocket(cs); continue; }
        ci->client = cs;
        ci->addr   = client;

        HANDLE th = CreateThread(NULL, 0, handle_client, ci, 0, NULL);
        if (th) CloseHandle(th);
        else { free(ci); closesocket(cs); }
    }

    DeleteCriticalSection(&g_log_cs);
    closesocket(srv);
    WSACleanup();
    return 0;
}
