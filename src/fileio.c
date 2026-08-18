#include "common.h"

const wchar_t* EncodingName(Encoding e) {
    switch (e) {
        case ENC_ANSI:     return L"ANSI";
        case ENC_UTF8:     return L"UTF-8";
        case ENC_UTF8_BOM: return L"UTF-8 BOM";
        case ENC_UTF16LE:  return L"UTF-16 LE";
        case ENC_UTF16BE:  return L"UTF-16 BE";
    }
    return L"?";
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
    if (h == INVALID_HANDLE_VALUE) { ShowError(L"无法打开文件。"); return NULL; }
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
    if (h == INVALID_HANDLE_VALUE) { ShowError(L"无法写入文件。"); free(bytes); free(conv); return FALSE; }
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
    if (h == INVALID_HANDLE_VALUE) { ShowError(L"无法打开文件。"); return NULL; }
    DWORD size = GetFileSize(h, NULL);
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
    if (h == INVALID_HANDLE_VALUE) { ShowError(L"无法写入文件。"); free(bytes); free(conv); return FALSE; }
    DWORD wr = 0;
    if (byteLen > 0) ok = WriteFile(h, bytes, byteLen, &wr, NULL);
    CloseHandle(h);
    free(bytes); free(conv);
    return ok && wr == byteLen;
}
