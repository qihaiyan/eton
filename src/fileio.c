#include "common.h"

/* 打开文件的大小上限：流式加载峰值内存约等于文件大小（旧全量路径 4~5 倍），
   1GB 上限与 Notepad++ 同级，超限仍拒绝 */
#define BIGFILE_LIMIT (1024ULL * 1024 * 1024)

const wchar_t* EncodingName(Encoding e) {
    /* 编码显示名与编码菜单/对话框共用一套文案（随界面语言切换，见 i18n.c） */
    if ((int)e < 0 || (int)e > ENC_UTF16BE) return L"?";
    return T((StrId)(STR_ENCNAME_ANSI + (int)e));
}

/* detect BOM / try utf8 */
Encoding DetectEncoding(const BYTE* data, DWORD size, BOOL* hasBom) {
    *hasBom = FALSE;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        *hasBom = TRUE; return ENC_UTF8_BOM;
    }
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        *hasBom = TRUE; return ENC_UTF16LE;
    }
    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        *hasBom = TRUE; return ENC_UTF16BE;
    }
    /* try utf8 validity */
    DWORD i = 0;
    while (i < size) {
        BYTE b = data[i];
        if (b < 0x80) { i++; continue; }
        else if ((b & 0xE0) == 0xC0) {
            if (i + 1 >= size) return ENC_ANSI;
            if ((data[i+1] & 0xC0) != 0x80) return ENC_ANSI;
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= size) return ENC_ANSI;
            if ((data[i+1] & 0xC0) != 0x80 || (data[i+2] & 0xC0) != 0x80) return ENC_ANSI;
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            if (i + 3 >= size) return ENC_ANSI;
            if ((data[i+1] & 0xC0) != 0x80 || (data[i+2] & 0xC0) != 0x80 || (data[i+3] & 0xC0) != 0x80) return ENC_ANSI;
            i += 4;
        } else {
            return ENC_ANSI;
        }
    }
    return ENC_UTF8;
}

static wchar_t* NormalizeNewlines(const wchar_t* src, DWORD len, int* outEol) {
    BOOL hasCRLF = FALSE, hasLF = FALSE, hasCR = FALSE;
    for (DWORD i = 0; i < len; i++) {
        if (src[i] == L'\r' && i + 1 < len && src[i+1] == L'\n') hasCRLF = TRUE;
        else if (src[i] == L'\n') hasLF = TRUE;
        else if (src[i] == L'\r') hasCR = TRUE;
    }
    if (hasCRLF) *outEol = 0;
    else if (hasLF) *outEol = 1;
    else if (hasCR) *outEol = 2;
    else *outEol = 1;   /* 内容不含行尾时默认 Unix (LF) */

    wchar_t* out = (wchar_t*)malloc((len * 2 + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    DWORD o = 0;
    for (DWORD i = 0; i < len; i++) {
        if (src[i] == L'\r' && i + 1 < len && src[i+1] == L'\n') {
            out[o++] = L'\r'; out[o++] = L'\n'; i++;
        } else if (src[i] == L'\n') {
            out[o++] = L'\r'; out[o++] = L'\n';
        } else if (src[i] == L'\r') {
            out[o++] = L'\r'; out[o++] = L'\n';
        } else {
            out[o++] = src[i];
        }
    }
    out[o] = L'\0';
    return out;
}

wchar_t* LoadFileToWStr(const wchar_t* path, Encoding enc, DWORD* outLenChars, int* outEol) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { ShowError(T(STR_MSG_CANT_OPEN)); return NULL; }
    DWORD size = GetFileSize(h, NULL);
    BYTE* raw = (BYTE*)malloc(size ? size : 1);
    if (!raw) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    if (size > 0) ReadFile(h, raw, size, &rd, NULL);
    CloseHandle(h);

    wchar_t* wtmp = NULL;
    DWORD wlen = 0;
    BOOL hasBom = FALSE;

    if (enc == ENC_ANSI) {
        int n = MultiByteToWideChar(CP_ACP, 0, (char*)raw, (int)size, NULL, 0);
        wtmp = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_ACP, 0, (char*)raw, (int)size, wtmp, n);
        wtmp[n] = L'\0'; wlen = n;
    } else if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        /* strip BOM if present */
        DWORD off = 0;
        if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { off = 3; hasBom = TRUE; }
        int n = MultiByteToWideChar(CP_UTF8, 0, (char*)(raw + off), (int)(size - off), NULL, 0);
        wtmp = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, (char*)(raw + off), (int)(size - off), wtmp, n);
        wtmp[n] = L'\0'; wlen = n;
    } else if (enc == ENC_UTF16LE) {
        DWORD off = 0;
        if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) { off = 2; hasBom = TRUE; }
        wlen = (size - off) / 2;
        wtmp = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
        memcpy(wtmp, raw + off, wlen * sizeof(wchar_t));
        wtmp[wlen] = L'\0';
    } else if (enc == ENC_UTF16BE) {
        DWORD off = 0;
        if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) { off = 2; hasBom = TRUE; }
        wlen = (size - off) / 2;
        wtmp = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
        for (DWORD i = 0; i < wlen; i++) {
            BYTE hi = raw[off + i*2], lo = raw[off + i*2 + 1];
            wtmp[i] = (wchar_t)((lo << 8) | hi);
        }
        wtmp[wlen] = L'\0';
    }
    free(raw);
    (void)hasBom;

    wchar_t* norm = NormalizeNewlines(wtmp, wlen, outEol);
    free(wtmp);
    if (!norm) return NULL;
    *outLenChars = (DWORD)wcslen(norm);
    return norm;
}

/* convert internal CRLF text to target eol, return new buffer (caller frees) */
wchar_t* ApplyEol(const wchar_t* text, DWORD len, int eol, DWORD* outLen) {
    wchar_t nl[3]; int nll;
    if (eol == 0) { nl[0]=L'\r'; nl[1]=L'\n'; nll=2; }
    else if (eol == 1) { nl[0]=L'\n'; nll=1; }
    else { nl[0]=L'\r'; nll=1; }
    wchar_t* out = (wchar_t*)malloc((len * 2 + 1) * sizeof(wchar_t));
    DWORD o = 0;
    for (DWORD i = 0; i < len; i++) {
        if (text[i] == L'\r' && i + 1 < len && text[i+1] == L'\n') {
            for (int k=0;k<nll;k++) out[o++]=nl[k];
            i++;
        } else if (text[i] == L'\n' || text[i] == L'\r') {
            for (int k=0;k<nll;k++) out[o++]=nl[k];
        } else out[o++] = text[i];
    }
    out[o] = L'\0';
    *outLen = o;
    return out;
}

BOOL SaveWStrToFile(const wchar_t* path, const wchar_t* text, DWORD lenChars, Encoding enc, int eol) {
    DWORD convLen = 0;
    wchar_t* conv = ApplyEol(text, lenChars, eol, &convLen);
    if (!conv) return FALSE;

    BYTE* bytes = NULL;
    DWORD byteLen = 0;
    BOOL ok = TRUE;

    if (enc == ENC_ANSI) {
        int n = WideCharToMultiByte(CP_ACP, 0, conv, (int)convLen, NULL, 0, NULL, NULL);
        bytes = (BYTE*)malloc(n + 1);
        WideCharToMultiByte(CP_ACP, 0, conv, (int)convLen, (char*)bytes, n, NULL, NULL);
        byteLen = n;
    } else if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        int n = WideCharToMultiByte(CP_UTF8, 0, conv, (int)convLen, NULL, 0, NULL, NULL);
        DWORD cap = n + (enc == ENC_UTF8_BOM ? 3 : 0);
        bytes = (BYTE*)malloc(cap + 1);
        DWORD off = 0;
        if (enc == ENC_UTF8_BOM) { bytes[0]=0xEF; bytes[1]=0xBB; bytes[2]=0xBF; off=3; }
        WideCharToMultiByte(CP_UTF8, 0, conv, (int)convLen, (char*)(bytes+off), n, NULL, NULL);
        byteLen = off + n;
    } else if (enc == ENC_UTF16LE) {
        byteLen = (convLen + 1) * 2;
        bytes = (BYTE*)malloc(byteLen);
        DWORD off = 0;
        bytes[0]=0xFF; bytes[1]=0xFE; off=2;
        memcpy(bytes + off, conv, convLen * 2);
        bytes[off + convLen*2] = 0; bytes[off + convLen*2 + 1] = 0;
    } else if (enc == ENC_UTF16BE) {
        byteLen = (convLen + 1) * 2;
        bytes = (BYTE*)malloc(byteLen);
        DWORD off = 0;
        bytes[0]=0xFE; bytes[1]=0xFF; off=2;
        for (DWORD i = 0; i < convLen; i++) {
            wchar_t c = conv[i];
            bytes[off + i*2] = (BYTE)(c >> 8);
            bytes[off + i*2 + 1] = (BYTE)(c & 0xFF);
        }
        bytes[off + convLen*2] = 0; bytes[off + convLen*2 + 1] = 0;
    }

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { ShowError(T(STR_MSG_CANT_WRITE)); free(bytes); free(conv); return FALSE; }
    DWORD wr = 0;
    if (byteLen > 0) ok = WriteFile(h, bytes, byteLen, &wr, NULL);
    CloseHandle(h);
    free(bytes);
    free(conv);
    return ok && wr == byteLen;
}

BOOL HasUnsupportedForAnsi(const wchar_t* text, DWORD len) {
    for (DWORD i = 0; i < len; i++) {
        if (text[i] > 0x00FF) return TRUE;
    }
    return FALSE;
}

/* ---------- Scintilla (UTF-8) file IO ---------- */

/* Load file and return UTF-8 text (caller frees). Detects encoding, converts
   everything to UTF-8. Also detects EOL type. */
char* LoadFileToUtf8(const wchar_t* path, Encoding* detectedEnc, int* outEol) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { ShowError(T(STR_MSG_CANT_OPEN)); return NULL; }
    DWORD size = GetFileSize(h, NULL);
    /* 超大文件保护：转换过程内存放大约 4~5 倍，超过上限直接拒绝 */
    if (size > BIGFILE_LIMIT) {
        wchar_t msg[128];
        wsprintf(msg, T(STR_MSG_FILE_TOO_BIG), (int)(BIGFILE_LIMIT / (1024 * 1024)));
        ShowError(msg);
        CloseHandle(h);
        return NULL;
    }
    BYTE* raw = (BYTE*)malloc(size ? size : 1);
    if (!raw) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    if (size > 0) ReadFile(h, raw, size, &rd, NULL);
    CloseHandle(h);

    /* Detect encoding from BOM / content */
    BOOL hasBom = FALSE;
    Encoding enc = DetectEncoding(raw, size, &hasBom);
    *detectedEnc = enc;

    /* Convert to wchar_t first (reuse existing logic via a temporary), then
       normalize EOL, then back to UTF-8. Simpler than handling every case
       in UTF-8 directly. */
    wchar_t* wtmp = NULL; DWORD wlen = 0;
    if (enc == ENC_ANSI) {
        int n = MultiByteToWideChar(CP_ACP, 0, (char*)raw, (int)size, NULL, 0);
        wtmp = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_ACP, 0, (char*)raw, (int)size, wtmp, n);
        wtmp[n] = L'\0'; wlen = n;
    } else if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        DWORD off = (hasBom && enc == ENC_UTF8_BOM) ? 3 : 0;
        int n = MultiByteToWideChar(CP_UTF8, 0, (char*)(raw + off), (int)(size - off), NULL, 0);
        wtmp = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, (char*)(raw + off), (int)(size - off), wtmp, n);
        wtmp[n] = L'\0'; wlen = n;
    } else if (enc == ENC_UTF16LE) {
        DWORD off = hasBom ? 2 : 0;
        wlen = (size - off) / 2;
        wtmp = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
        memcpy(wtmp, raw + off, wlen * sizeof(wchar_t));
        wtmp[wlen] = L'\0';
    } else if (enc == ENC_UTF16BE) {
        DWORD off = hasBom ? 2 : 0;
        wlen = (size - off) / 2;
        wtmp = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
        for (DWORD i = 0; i < wlen; i++) {
            BYTE hi = raw[off + i*2], lo = raw[off + i*2 + 1];
            wtmp[i] = (wchar_t)((lo << 8) | hi);
        }
        wtmp[wlen] = L'\0';
    }
    free(raw);

    if (!wtmp) return NULL;

    /* Normalize EOL and detect type */
    int eol = 0;
    wchar_t* norm = NormalizeNewlines(wtmp, wlen, &eol);
    free(wtmp);
    if (!norm) return NULL;
    *outEol = eol;

    /* Convert normalized wchar_t -> UTF-8 */
    DWORD nlen = (DWORD)wcslen(norm);
    int u8n = WideCharToMultiByte(CP_UTF8, 0, norm, (int)nlen, NULL, 0, NULL, NULL);
    char* u8 = (char*)malloc(u8n + 1);
    if (!u8) { free(norm); return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, norm, (int)nlen, u8, u8n, NULL, NULL);
    u8[u8n] = '\0';
    free(norm);
    return u8;
}

/* Save UTF-8 text to file with given encoding + EOL conversion.
   The text parameter is UTF-8 (from Scintilla). */
BOOL SaveUtf8ToFile(const wchar_t* path, const char* text, DWORD len, Encoding enc, int eol) {
    /* UTF-8 -> wchar_t for EOL conversion via existing ApplyEol */
    int wn = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, NULL, 0);
    wchar_t* w = (wchar_t*)malloc((wn + 1) * sizeof(wchar_t));
    if (!w) return FALSE;
    MultiByteToWideChar(CP_UTF8, 0, text, (int)len, w, wn);
    w[wn] = L'\0';

    DWORD convLen = 0;
    wchar_t* conv = ApplyEol(w, wn, eol, &convLen);
    free(w);
    if (!conv) return FALSE;

    /* Now convert wchar_t to target encoding bytes */
    BYTE* bytes = NULL; DWORD byteLen = 0; BOOL ok = TRUE;

    if (enc == ENC_ANSI) {
        int n = WideCharToMultiByte(CP_ACP, 0, conv, (int)convLen, NULL, 0, NULL, NULL);
        bytes = (BYTE*)malloc(n + 1);
        WideCharToMultiByte(CP_ACP, 0, conv, (int)convLen, (char*)bytes, n, NULL, NULL);
        byteLen = n;
    } else if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        int n = WideCharToMultiByte(CP_UTF8, 0, conv, (int)convLen, NULL, 0, NULL, NULL);
        DWORD cap = n + (enc == ENC_UTF8_BOM ? 3 : 0);
        bytes = (BYTE*)malloc(cap + 1);
        DWORD off = 0;
        if (enc == ENC_UTF8_BOM) { bytes[0]=0xEF; bytes[1]=0xBB; bytes[2]=0xBF; off=3; }
        WideCharToMultiByte(CP_UTF8, 0, conv, (int)convLen, (char*)(bytes+off), n, NULL, NULL);
        byteLen = off + n;
    } else if (enc == ENC_UTF16LE) {
        byteLen = convLen * 2;
        bytes = (BYTE*)malloc(byteLen + 2);
        bytes[0]=0xFF; bytes[1]=0xFE;
        memcpy(bytes + 2, conv, byteLen);
        byteLen += 2;
    } else if (enc == ENC_UTF16BE) {
        byteLen = convLen * 2;
        bytes = (BYTE*)malloc(byteLen + 2);
        bytes[0]=0xFE; bytes[1]=0xFF;
        for (DWORD i = 0; i < convLen; i++) {
            wchar_t c = conv[i];
            bytes[2 + i*2] = (BYTE)(c >> 8);
            bytes[2 + i*2 + 1] = (BYTE)(c & 0xFF);
        }
        byteLen += 2;
    }

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { ShowError(T(STR_MSG_CANT_WRITE)); free(bytes); free(conv); return FALSE; }
    DWORD wr = 0;
    if (byteLen > 0) ok = WriteFile(h, bytes, byteLen, &wr, NULL);
    CloseHandle(h);
    free(bytes); free(conv);
    return ok && wr == byteLen;
}

/* ---------- 流式大文件 IO ----------
   加载：分块读取 -> 在字符安全边界切分 -> 逐块转码 + EOL 规范化(统一 CRLF，
   与全量路径语义一致) -> SCI_APPENDTEXT 追加进 Scintilla。峰值内存 ~40MB 常量
   缓冲 + 文档本身，不再随文件大小产生多份全量副本。
   保存：SCI_GETTEXTRANGEFULL 分块取出 -> EOL 转换 + 编码 -> 写同目录临时文件
   -> 成功后 MoveFileEx 原子替换；取消/失败只删临时文件，原文件不受影响
   (旧路径直接 CREATE_ALWAYS 重写，中途失败会把原文件截断)。 */

#define IO_CHUNK    (4u * 1024 * 1024)     /* 每块 4MB */
#define ENC_PROBE_MAX (512 * 1024)         /* 编码探测只看文件前缀(旧全量扫描对 GB 级文件过慢) */

/* 本块内可安全处理的字节数：优先切在最后一个换行后；否则退到不拆
   多字节字符/代理对的位置；EOF 时(对齐后)全量处理 */
static DWORD SafeCut(Encoding enc, const BYTE* raw, DWORD have, BOOL eof) {
    DWORD cut;
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        cut = 0;
        for (DWORD i = have & ~(DWORD)1; i >= 2; i -= 2) {
            unsigned u = (enc == ENC_UTF16LE) ? (unsigned)(raw[i-2] | (raw[i-1] << 8))
                                              : (unsigned)(raw[i-1] | (raw[i-2] << 8));
            if (u == 0x000A) { cut = i; break; }
        }
        if (cut == 0) cut = have & ~(DWORD)1;      /* 无换行:按 16 位单元对齐切 */
        if (cut >= 2 && !eof) {                     /* 末尾是高位代理则让出一对 */
            unsigned u = (enc == ENC_UTF16LE) ? (unsigned)(raw[cut-2] | (raw[cut-1] << 8))
                                              : (unsigned)(raw[cut-1] | (raw[cut-2] << 8));
            if (u >= 0xD800 && u <= 0xDBFF) cut -= 2;
        }
        return cut;
    }
    cut = 0;
    for (DWORD i = have; i > 0; i--) {
        if (raw[i-1] == 0x0A) { cut = i; break; }
    }
    if (cut > 0 || eof) return (cut > 0) ? cut : have;
    /* 无换行的超长单行：退到不拆字符的位置(0x0A 不会出现在续字节/双字节字符中) */
    cut = have;
    if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        while (cut > 0 && (raw[cut-1] & 0xC0) == 0x80) cut--;   /* 跳过 UTF-8 续字节 */
        if (cut > 0 && raw[cut-1] >= 0xC0) cut--;               /* 连可能残缺的首字节一并留给下一块 */
    } else {
        if (cut > 0 && IsDBCSLeadByte(raw[cut-1])) cut--;       /* ANSI:末字节是双字节首字节 */
    }
    return cut;
}

/* 追加一段 EOL 规范化(统一 CRLF)文本：UTF-8 字节级处理(0x0A/0x0D 不会出现在
   UTF-8 多字节序列中)。pendCR 跨块配对 CRLF，同时统计 EOL 类型 */
static DWORD NormalizeEolBytes(const BYTE* src, DWORD len, char* out,
                               BOOL* pendCR, BOOL* hasCRLF, BOOL* hasLF, BOOL* hasCR) {
    DWORD o = 0;
    for (DWORD i = 0; i < len; i++) {
        BYTE c = src[i];
        if (c == '\n') {
            if (*pendCR) { *hasCRLF = TRUE; *pendCR = FALSE; } else *hasLF = TRUE;
            out[o++] = '\r'; out[o++] = '\n';
        } else if (c == '\r') {
            if (*pendCR) { *hasCR = TRUE; out[o++] = '\r'; out[o++] = '\n'; }
            *pendCR = TRUE;
        } else {
            if (*pendCR) { *hasCR = TRUE; out[o++] = '\r'; out[o++] = '\n'; *pendCR = FALSE; }
            out[o++] = (char)c;
        }
    }
    return o;
}

/* 同上，wchar 版（供 ANSI / UTF-16 路径使用） */
static DWORD NormalizeEolW(const wchar_t* src, DWORD len, wchar_t* out,
                           BOOL* pendCR, BOOL* hasCRLF, BOOL* hasLF, BOOL* hasCR) {
    DWORD o = 0;
    for (DWORD i = 0; i < len; i++) {
        wchar_t c = src[i];
        if (c == L'\n') {
            if (*pendCR) { *hasCRLF = TRUE; *pendCR = FALSE; } else *hasLF = TRUE;
            out[o++] = L'\r'; out[o++] = L'\n';
        } else if (c == L'\r') {
            if (*pendCR) { *hasCR = TRUE; out[o++] = L'\r'; out[o++] = L'\n'; }
            *pendCR = TRUE;
        } else {
            if (*pendCR) { *hasCR = TRUE; out[o++] = L'\r'; out[o++] = L'\n'; *pendCR = FALSE; }
            out[o++] = c;
        }
    }
    return o;
}

BOOL StreamLoadToDoc(HWND hed, const wchar_t* path, Encoding enc,
                     Encoding* outEnc, int* outEol,
                     IoProgressFn progress, void* ctx) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { ShowError(T(STR_MSG_CANT_OPEN)); return FALSE; }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return FALSE; }
    UINT64 total = (UINT64)sz.QuadPart;
    if (total > BIGFILE_LIMIT) {
        wchar_t msg[128];
        wsprintf(msg, T(STR_MSG_FILE_TOO_BIG), (int)(BIGFILE_LIMIT / (1024 * 1024)));
        ShowError(msg);
        CloseHandle(h);
        return FALSE;
    }

    BYTE*    raw = (BYTE*)malloc(IO_CHUNK + 8);
    wchar_t* w   = (wchar_t*)malloc((IO_CHUNK + 8) * sizeof(wchar_t));        /* 块 -> wchar */
    wchar_t* wn  = (wchar_t*)malloc((2 * IO_CHUNK + 8) * sizeof(wchar_t));    /* EOL 规范化后 */
    char*    out = (char*)malloc(3 * IO_CHUNK + 16);                          /* UTF-8 输出 */
    BOOL ok = FALSE;
    DWORD have = 0;
    BOOL eof = FALSE, pendCR = FALSE;
    BOOL hasCRLF = FALSE, hasLF = FALSE, hasCR = FALSE;
    UINT64 done = 0;

    if (!raw || !w || !wn || !out) goto cleanup;

    /* ---- 首块 + BOM/编码探测 ---- */
    {
        DWORD want = (total < (UINT64)IO_CHUNK) ? (DWORD)total : IO_CHUNK;
        DWORD got = 0;
        while (got < want) {
            DWORD part = 0;
            if (!ReadFile(h, raw + got, want - got, &part, NULL) || part == 0) break;
            got += part;
        }
        have = got;
        if (got < want) eof = TRUE;

        DWORD bomLen = 0;
        if (enc == ENC_AUTO) {
            if (have >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
                enc = ENC_UTF8_BOM; bomLen = 3;
            } else if (have >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
                enc = ENC_UTF16LE; bomLen = 2;
            } else if (have >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
                enc = ENC_UTF16BE; bomLen = 2;
            } else {
                DWORD n = have; if (n > ENC_PROBE_MAX) n = ENC_PROBE_MAX;
                /* 探测窗口回退到字符边界：截断在多字节字符中间会被当成非法序列误判为 ANSI */
                while (n > 0 && (raw[n-1] & 0xC0) == 0x80) n--;
                if (n > 0 && raw[n-1] >= 0xC0) n--;
                BOOL dummy = FALSE;
                enc = (DetectEncoding(raw, n, &dummy) == ENC_UTF8) ? ENC_UTF8 : ENC_ANSI;
            }
        } else {
            /* 指定编码打开：跳过匹配的 BOM */
            if (enc == ENC_UTF8_BOM && have >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) bomLen = 3;
            else if (enc == ENC_UTF16LE && have >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) bomLen = 2;
            else if (enc == ENC_UTF16BE && have >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) bomLen = 2;
        }
        if (bomLen) { memmove(raw, raw + bomLen, have - bomLen); have -= bomLen; }
        done = bomLen;
    }
    if (outEnc) *outEnc = enc;

    /* ---- 分块主循环 ---- */
    for (;;) {
        if (!eof && have < IO_CHUNK) {
            DWORD want = IO_CHUNK - have, got = 0;
            if (!ReadFile(h, raw + have, want, &got, NULL)) goto cleanup;
            have += got;
            if (got < want) eof = TRUE;
        }
        if (have == 0 && eof) break;
        DWORD cut = SafeCut(enc, raw, have, eof);
        if (cut == 0) { if (eof) break; else continue; }

        DWORD outLen = 0;
        if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
            outLen = NormalizeEolBytes(raw, cut, out, &pendCR, &hasCRLF, &hasLF, &hasCR);
        } else {
            DWORD n = 0;
            if (enc == ENC_ANSI) {
                n = (DWORD)MultiByteToWideChar(CP_ACP, 0, (LPCCH)raw, (int)cut, w, IO_CHUNK + 8);
            } else {   /* UTF-16 LE/BE -> wchar */
                n = cut / 2;
                for (DWORD i = 0; i < n; i++) {
                    w[i] = (enc == ENC_UTF16LE) ? (wchar_t)(raw[i*2] | (raw[i*2+1] << 8))
                                                : (wchar_t)(raw[i*2+1] | (raw[i*2] << 8));
                }
            }
            DWORD wn2 = NormalizeEolW(w, n, wn, &pendCR, &hasCRLF, &hasLF, &hasCR);
            int m = WideCharToMultiByte(CP_UTF8, 0, wn, (int)wn2, out, (int)(3 * IO_CHUNK + 16), NULL, NULL);
            if (n > 0 && m <= 0) goto cleanup;
            outLen = (DWORD)m;
        }
        if (outLen) SendMessage(hed, SCI_APPENDTEXT, (WPARAM)outLen, (LPARAM)out);
        done += cut;
        if (cut < have) memmove(raw, raw + cut, have - cut);
        have -= cut;

        if (progress && !progress(done, total, ctx)) goto cleanup;   /* 取消 */
    }

    /* 末尾悬挂的孤立 CR 补成 CRLF */
    if (pendCR) {
        hasCR = TRUE;
        out[0] = '\r'; out[1] = '\n';
        SendMessage(hed, SCI_APPENDTEXT, 2, (LPARAM)out);
    }

    /* 旧版把 UTF-16 保存时写入的结尾 NUL 当内容读入，这里兼容去掉 */
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        Sci_Position len = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
        if (len >= 1 && SendMessage(hed, SCI_GETCHARAT, (WPARAM)(len - 1), 0) == 0)
            SendMessage(hed, SCI_DELETERANGE, (WPARAM)(len - 1), (Sci_Position)1);
    }

    if (outEol) *outEol = hasCRLF ? 0 : (hasLF ? 1 : (hasCR ? 2 : 1));
    ok = TRUE;

cleanup:
    free(raw); free(w); free(wn); free(out);
    CloseHandle(h);
    return ok;
}

/* 按目标行尾追加一个换行序列到 wchar 缓冲 */
static void EmitEol(wchar_t* wn, DWORD* o, int eol) {
    if (eol == 0) { wn[(*o)++] = L'\r'; wn[(*o)++] = L'\n'; }
    else wn[(*o)++] = (eol == 1) ? L'\n' : L'\r';
}

BOOL StreamSaveFromDoc(HWND hed, const wchar_t* path, Encoding enc, int eol,
                       IoProgressFn progress, void* ctx) {
    wchar_t tmp[MAX_PATH + 8];
    if (_snwprintf(tmp, MAX_PATH + 8, L"%s.tmp", path) < 0) return FALSE;
    tmp[MAX_PATH + 7] = L'\0';

    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { ShowError(T(STR_MSG_CANT_WRITE)); return FALSE; }

    char*    buf = (char*)malloc(IO_CHUNK + 8);   /* GETTEXTRANGE 会多写一个 NUL 结尾字节 */
    wchar_t* w   = (wchar_t*)malloc((IO_CHUNK + 8) * sizeof(wchar_t));
    wchar_t* wn  = (wchar_t*)malloc((2 * IO_CHUNK + 8) * sizeof(wchar_t));
    char*    out = (char*)malloc(4 * IO_CHUNK + 16);
    BOOL ok = FALSE, pendCR = FALSE;
    Sci_Position total = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    Sci_Position pos = 0;

    if (!buf || !w || !wn || !out) goto cleanup;

    /* BOM */
    {
        BYTE bom[3]; DWORD bomLen = 0;
        if (enc == ENC_UTF8_BOM) { bom[0]=0xEF; bom[1]=0xBB; bom[2]=0xBF; bomLen = 3; }
        else if (enc == ENC_UTF16LE) { bom[0]=0xFF; bom[1]=0xFE; bomLen = 2; }
        else if (enc == ENC_UTF16BE) { bom[0]=0xFE; bom[1]=0xFF; bomLen = 2; }
        if (bomLen) {
            DWORD wr = 0;
            if (!WriteFile(h, bom, bomLen, &wr, NULL) || wr != bomLen) goto cleanup;
        }
    }

    while (pos < total) {
        Sci_Position take = total - pos;
        if (take > IO_CHUNK) take = IO_CHUNK;
        struct Sci_TextRangeFull tr;
        tr.chrg.cpMin = pos; tr.chrg.cpMax = pos + take; tr.lpstrText = buf;
        SendMessage(hed, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);
        if (pos + take < total) {
            /* 非末块：退到最后一个换行，避免在 UTF-8 多字节字符中间切开；
               超长单行则退掉不完整的尾部序列（char 默认有符号，字节比较须转无符号） */
            while (take > 0 && buf[take-1] != '\n') take--;
            if (take == 0) {
                take = (total - pos < IO_CHUNK) ? total - pos : IO_CHUNK;
                while (take > 0 && ((unsigned char)buf[take-1] & 0xC0) == 0x80) take--;
                if (take > 0 && (unsigned char)buf[take-1] >= 0xC0) take--;
            }
        }
        pos += take;

        int n = 0;
        if (take > 0) n = MultiByteToWideChar(CP_UTF8, 0, buf, (int)take, w, IO_CHUNK + 8);
        if (take > 0 && n <= 0) goto cleanup;

        /* 任意行尾 -> 目标序列(pendCR 跨块配对 CRLF) */
        DWORD wn2 = 0;
        for (int i = 0; i < n; i++) {
            wchar_t c = w[i];
            if (c == L'\n') {
                pendCR = FALSE;
                EmitEol(wn, &wn2, eol);
            } else if (c == L'\r') {
                if (pendCR) EmitEol(wn, &wn2, eol);
                pendCR = TRUE;
            } else {
                if (pendCR) { EmitEol(wn, &wn2, eol); pendCR = FALSE; }
                wn[wn2++] = c;
            }
        }

        DWORD outLen = 0;
        if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
            int m = WideCharToMultiByte(CP_UTF8, 0, wn, (int)wn2, out, (int)(4 * IO_CHUNK + 16), NULL, NULL);
            if (wn2 > 0 && m <= 0) goto cleanup;
            outLen = (DWORD)m;
        } else if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
            BYTE* pb = (BYTE*)out;
            for (DWORD i = 0; i < wn2; i++) {
                if (enc == ENC_UTF16LE) { pb[outLen++] = (BYTE)(wn[i] & 0xFF);  pb[outLen++] = (BYTE)(wn[i] >> 8); }
                else                    { pb[outLen++] = (BYTE)(wn[i] >> 8);     pb[outLen++] = (BYTE)(wn[i] & 0xFF); }
            }
        } else {   /* ANSI：无法表示的字符替换为 '?'（保存前 ConfirmAnsiOk 已提示） */
            int m = WideCharToMultiByte(CP_ACP, 0, wn, (int)wn2, out, (int)(4 * IO_CHUNK), "?", NULL);
            if (wn2 > 0 && m <= 0) goto cleanup;
            outLen = (DWORD)m;
        }
        if (outLen) {
            DWORD wr = 0;
            if (!WriteFile(h, out, outLen, &wr, NULL) || wr != outLen) goto cleanup;
        }
        if (progress && !progress((UINT64)pos, (UINT64)total, ctx)) goto cleanup;   /* 取消 */
    }

    /* 末尾悬挂的孤立 CR */
    if (pendCR) {
        BYTE tb[4]; DWORD tl = 0;
        if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
            wchar_t u[2]; int un = 0;
            if (eol == 0) { u[un++] = L'\r'; u[un++] = L'\n'; }
            else u[un++] = (eol == 1) ? L'\n' : L'\r';
            for (int i = 0; i < un; i++) {
                if (enc == ENC_UTF16LE) { tb[tl++] = (BYTE)(u[i] & 0xFF);  tb[tl++] = (BYTE)(u[i] >> 8); }
                else                    { tb[tl++] = (BYTE)(u[i] >> 8);     tb[tl++] = (BYTE)(u[i] & 0xFF); }
            }
        } else {
            if (eol == 0) { tb[tl++] = 0x0D; tb[tl++] = 0x0A; }
            else tb[tl++] = (eol == 1) ? 0x0A : 0x0D;
        }
        DWORD wr = 0;
        if (!WriteFile(h, tb, tl, &wr, NULL) || wr != tl) goto cleanup;
    }

    ok = TRUE;

cleanup:
    CloseHandle(h);
    free(buf); free(w); free(wn); free(out);
    if (!ok) { DeleteFileW(tmp); return FALSE; }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp); return FALSE; }
    return TRUE;
}
