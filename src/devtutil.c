/* devtutil.c — 开发者工具的纯算法层
   （Base64 / URL / Unicode 编解码、CNG 哈希、UUID v4）。
   刻意不引用 common.h 与任何应用全局状态，便于独立编译做向量自测。 */

#include <windows.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

/* ---------------- Base64 ---------------- */

static const char kB64Tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* DevUtil_B64Encode(const char* in, size_t n, size_t* outN) {
    size_t m = (n + 2) / 3 * 4;
    char* o = (char*)malloc(m + 1);
    if (!o) return NULL;
    size_t i = 0, k = 0;
    while (i + 3 <= n) {
        unsigned v = ((unsigned)(unsigned char)in[i] << 16) |
                     ((unsigned)(unsigned char)in[i + 1] << 8) |
                      (unsigned)(unsigned char)in[i + 2];
        o[k++] = kB64Tab[(v >> 18) & 63];
        o[k++] = kB64Tab[(v >> 12) & 63];
        o[k++] = kB64Tab[(v >> 6) & 63];
        o[k++] = kB64Tab[v & 63];
        i += 3;
    }
    size_t r = n - i;
    if (r == 1) {
        unsigned v = (unsigned)(unsigned char)in[i] << 16;
        o[k++] = kB64Tab[(v >> 18) & 63];
        o[k++] = kB64Tab[(v >> 12) & 63];
        o[k++] = '=';
        o[k++] = '=';
    } else if (r == 2) {
        unsigned v = ((unsigned)(unsigned char)in[i] << 16) |
                     ((unsigned)(unsigned char)in[i + 1] << 8);
        o[k++] = kB64Tab[(v >> 18) & 63];
        o[k++] = kB64Tab[(v >> 12) & 63];
        o[k++] = kB64Tab[(v >> 6) & 63];
        o[k++] = '=';
    }
    o[k] = '\0';
    if (outN) *outN = k;
    return o;
}

/* 解码同时接受标准与 URL-safe（- _）字母表；忽略空白；
   '=' 只允许出现在收尾（至多两个）；失败时 errPos 为输入中的字节下标。 */
static int B6Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

BOOL DevUtil_B64Decode(const char* in, size_t n, char** out,
                       size_t* outN, size_t* errPos) {
    *out = NULL;
    if (outN) *outN = 0;
    if (errPos) *errPos = 0;
    char* o = (char*)malloc(n + 1);
    if (!o) return FALSE;
#define B64FAIL(idx) do { if (errPos) *errPos = (size_t)(idx); free(o); return FALSE; } while (0)
    unsigned acc = 0;
    int nb = 0;
    size_t k = 0, pads = 0;
    BOOL padSeen = FALSE;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '=') {
            if (++pads > 2) B64FAIL(i);
            if (!padSeen) {
                if (nb == 3) { o[k++] = (char)(acc >> 10); o[k++] = (char)((acc >> 2) & 0xFF); }
                else if (nb == 2) { o[k++] = (char)(acc >> 4); }
                else B64FAIL(i);
                acc = 0; nb = 0;
                padSeen = TRUE;
            }
            continue;
        }
        if (padSeen) B64FAIL(i);
        int v = B6Val(c);
        if (v < 0) B64FAIL(i);
        acc = (acc << 6) | (unsigned)v;
        if (++nb == 4) {
            o[k++] = (char)(acc >> 16);
            o[k++] = (char)(acc >> 8);
            o[k++] = (char)acc;
            acc = 0; nb = 0;
        }
    }
    if (!padSeen) {
        if (nb == 2) o[k++] = (char)(acc >> 4);
        else if (nb == 3) { o[k++] = (char)(acc >> 10); o[k++] = (char)((acc >> 2) & 0xFF); }
        else if (nb == 1) B64FAIL(n);
    }
    o[k] = '\0';
#undef B64FAIL
    *out = o;
    if (outN) *outN = k;
    return TRUE;
}

/* ---------------- URL (RFC 3986) ---------------- */

static BOOL UrlUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

char* DevUtil_UrlEncode(const char* in, size_t n, size_t* outN) {
    static const char hx[] = "0123456789ABCDEF";
    char* o = (char*)malloc(n * 3 + 1);
    if (!o) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (UrlUnreserved(c)) {
            o[k++] = (char)c;
        } else {
            o[k++] = '%';
            o[k++] = hx[c >> 4];
            o[k++] = hx[c & 15];
        }
    }
    o[k] = '\0';
    if (outN) *outN = k;
    return o;
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

BOOL DevUtil_UrlDecode(const char* in, size_t n, char** out,
                       size_t* outN, size_t* errPos) {
    *out = NULL;
    if (outN) *outN = 0;
    if (errPos) *errPos = 0;
    char* o = (char*)malloc(n + 1);
    if (!o) return FALSE;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == '+') {
            o[k++] = ' ';
        } else if (c == '%') {
            if (i + 2 >= n || HexVal(in[i + 1]) < 0 || HexVal(in[i + 2]) < 0) {
                if (errPos) *errPos = i;
                free(o);
                return FALSE;
            }
            o[k++] = (char)(HexVal(in[i + 1]) * 16 + HexVal(in[i + 2]));
            i += 2;
        } else {
            o[k++] = c;
        }
    }
    o[k] = '\0';
    *out = o;
    if (outN) *outN = k;
    return TRUE;
}

/* ---------------- Unicode 转义 (\uXXXX) ---------------- */

static size_t Utf8Enc(unsigned cp, char* o) {
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6));
        o[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 63));
        o[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 63));
    o[2] = (char)(0x80 | ((cp >> 6) & 63));
    o[3] = (char)(0x80 | (cp & 63));
    return 4;
}

/* 可打印 ASCII 原样；'\' → \\；\n \t \r 用短转义；其余 → \uXXXX。
   wchar_t 即 UTF-16 单元，非 BMP 字符自然输出为一对代理转义。 */
char* DevUtil_Escape(const char* in, size_t n, size_t* outN) {
    static const char hx[] = "0123456789abcdef";
    char* o = (char*)malloc(n * 6 + 8);
    if (!o) return NULL;
    size_t k = 0;
    if (n) {
        int wn = MultiByteToWideChar(CP_UTF8, 0, in, (int)n, NULL, 0);
        if (wn <= 0) { free(o); return NULL; }
        wchar_t* w = (wchar_t*)malloc((size_t)wn * sizeof(wchar_t) + 2);
        if (!w) { free(o); return NULL; }
        MultiByteToWideChar(CP_UTF8, 0, in, (int)n, w, wn);
        for (int i = 0; i < wn; i++) {
            wchar_t u = w[i];
            if (u == L'\\') { o[k++] = '\\'; o[k++] = '\\'; }
            else if (u == L'\n') { o[k++] = '\\'; o[k++] = 'n'; }
            else if (u == L'\t') { o[k++] = '\\'; o[k++] = 't'; }
            else if (u == L'\r') { o[k++] = '\\'; o[k++] = 'r'; }
            else if (u >= 0x20 && u <= 0x7E) { o[k++] = (char)u; }
            else {
                o[k++] = '\\'; o[k++] = 'u';
                o[k++] = hx[(u >> 12) & 15];
                o[k++] = hx[(u >> 8) & 15];
                o[k++] = hx[(u >> 4) & 15];
                o[k++] = hx[u & 15];
            }
        }
        free(w);
    }
    o[k] = '\0';
    if (outN) *outN = k;
    return o;
}

/* 支持 \uXXXX（含代理对）、\n \t \r \b \f \" \\ \/；
   严格模式：非法转义、孤立代理、截断的 \u 均报错。 */
BOOL DevUtil_Unescape(const char* in, size_t n, char** out,
                      size_t* outN, size_t* errPos) {
    *out = NULL;
    if (outN) *outN = 0;
    if (errPos) *errPos = 0;
    char* o = (char*)malloc(n + 1);
    if (!o) return FALSE;
#define UNFAIL(idx) do { if (errPos) *errPos = (size_t)(idx); free(o); return FALSE; } while (0)
    size_t i = 0, k = 0;
    while (i < n) {
        char c = in[i];
        if (c != '\\') { o[k++] = c; i++; continue; }
        if (i + 1 >= n) UNFAIL(i);
        char e = in[i + 1];
        if (e == 'n') { o[k++] = '\n'; i += 2; }
        else if (e == 't') { o[k++] = '\t'; i += 2; }
        else if (e == 'r') { o[k++] = '\r'; i += 2; }
        else if (e == 'b') { o[k++] = '\b'; i += 2; }
        else if (e == 'f') { o[k++] = '\f'; i += 2; }
        else if (e == '"') { o[k++] = '"'; i += 2; }
        else if (e == '\\') { o[k++] = '\\'; i += 2; }
        else if (e == '/') { o[k++] = '/'; i += 2; }
        else if (e == 'u') {
            if (i + 5 >= n) UNFAIL(i);
            unsigned u1 = 0;
            for (int j = 0; j < 4; j++) {
                int v = HexVal(in[i + 2 + j]);
                if (v < 0) UNFAIL(i + 2 + j);
                u1 = u1 * 16 + (unsigned)v;
            }
            unsigned cp;
            if (u1 >= 0xD800 && u1 <= 0xDBFF) {
                if (i + 11 >= n || in[i + 6] != '\\' || in[i + 7] != 'u') UNFAIL(i + 6);
                unsigned u2 = 0;
                for (int j = 0; j < 4; j++) {
                    int v = HexVal(in[i + 8 + j]);
                    if (v < 0) UNFAIL(i + 8 + j);
                    u2 = u2 * 16 + (unsigned)v;
                }
                if (u2 < 0xDC00 || u2 > 0xDFFF) UNFAIL(i + 8);
                cp = 0x10000 + ((u1 - 0xD800) << 10) + (u2 - 0xDC00);
                k += Utf8Enc(cp, o + k);
                i += 12;
            } else if (u1 >= 0xDC00 && u1 <= 0xDFFF) {
                UNFAIL(i);                     /* 孤立低位代理 */
            } else {
                k += Utf8Enc(u1, o + k);
                i += 6;
            }
        } else {
            UNFAIL(i + 1);
        }
    }
    o[k] = '\0';
#undef UNFAIL
    *out = o;
    if (outN) *outN = k;
    return TRUE;
}

/* ---------------- CNG 哈希 / UUID ---------------- */

BOOL DevUtil_Hash(const wchar_t* alg, const BYTE* data, DWORD len,
                  BYTE* out, DWORD outLen) {
    BCRYPT_ALG_HANDLE ha = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    BOOL ok = FALSE;
    if (BCryptOpenAlgorithmProvider(&ha, alg, NULL, 0) == 0) {
        if (BCryptCreateHash(ha, &hh, NULL, 0, NULL, 0, 0) == 0) {
            if (BCryptHashData(hh, (PUCHAR)data, len, 0) == 0 &&
                BCryptFinishHash(hh, out, outLen, 0) == 0)
                ok = TRUE;
            BCryptDestroyHash(hh);
        }
        BCryptCloseAlgorithmProvider(ha, 0);
    }
    return ok;
}

BOOL DevUtil_UuidV4(char out[37]) {
    BYTE b[16];
    if (BCryptGenRandom(NULL, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return FALSE;
    b[6] = (BYTE)((b[6] & 0x0F) | 0x40);   /* version 4 */
    b[8] = (BYTE)((b[8] & 0x3F) | 0x80);   /* RFC 4122 variant */
    static const char hx[] = "0123456789abcdef";
    int k = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[k++] = '-';
        out[k++] = hx[b[i] >> 4];
        out[k++] = hx[b[i] & 15];
    }
    out[k] = '\0';
    return TRUE;
}
