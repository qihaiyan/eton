#include "common.h"

#define BIGFILE_LIMIT (1024ULL * 1024 * 1024)

const wchar_t* EncodingName(Encoding e) {
    if ((int)e < 0 || (int)e > ENC_UTF16BE) return L"?";
    return T((StrId)(STR_ENCNAME_ANSI + (int)e));
}

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

#define IO_CHUNK    (4u * 1024 * 1024)
#define ENC_PROBE_MAX (512 * 1024)

static DWORD SafeCut(Encoding enc, const BYTE* raw, DWORD have, BOOL eof) {
    DWORD cut;
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        cut = 0;
        for (DWORD i = have & ~(DWORD)1; i >= 2; i -= 2) {
            unsigned u = (enc == ENC_UTF16LE) ? (unsigned)(raw[i-2] | (raw[i-1] << 8))
                                              : (unsigned)(raw[i-1] | (raw[i-2] << 8));
            if (u == 0x000A) { cut = i; break; }
        }
        if (cut == 0) cut = have & ~(DWORD)1;
        if (cut >= 2 && !eof) {
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
    cut = have;
    if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        while (cut > 0 && (raw[cut-1] & 0xC0) == 0x80) cut--;
        if (cut > 0 && raw[cut-1] >= 0xC0) cut--;
    } else {
        if (cut > 0 && IsDBCSLeadByte(raw[cut-1])) cut--;
    }
    return cut;
}

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
        WFmt(msg, T(STR_MSG_FILE_TOO_BIG), (int)(BIGFILE_LIMIT / (1024 * 1024)));
        ShowError(msg);
        CloseHandle(h);
        return FALSE;
    }

    BYTE*    raw = (BYTE*)malloc(IO_CHUNK + 8);
    wchar_t* w   = (wchar_t*)malloc((IO_CHUNK + 8) * sizeof(wchar_t));
    wchar_t* wn  = (wchar_t*)malloc((2 * IO_CHUNK + 8) * sizeof(wchar_t));
    char*    out = (char*)malloc(3 * IO_CHUNK + 16);
    BOOL ok = FALSE;
    DWORD have = 0;
    BOOL eof = FALSE, pendCR = FALSE;
    BOOL hasCRLF = FALSE, hasLF = FALSE, hasCR = FALSE;
    UINT64 done = 0;

    if (!raw || !w || !wn || !out) goto cleanup;

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
                while (n > 0 && (raw[n-1] & 0xC0) == 0x80) n--;
                if (n > 0 && raw[n-1] >= 0xC0) n--;
                BOOL dummy = FALSE;
                enc = (DetectEncoding(raw, n, &dummy) == ENC_UTF8) ? ENC_UTF8 : ENC_ANSI;
            }
        } else {
            if (enc == ENC_UTF8_BOM && have >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) bomLen = 3;
            else if (enc == ENC_UTF16LE && have >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) bomLen = 2;
            else if (enc == ENC_UTF16BE && have >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) bomLen = 2;
        }
        if (bomLen) { memmove(raw, raw + bomLen, have - bomLen); have -= bomLen; }
        done = bomLen;
    }
    if (outEnc) *outEnc = enc;

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
            } else {
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

        if (progress && !progress(done, total, ctx)) goto cleanup;
    }

    if (pendCR) {
        hasCR = TRUE;
        out[0] = '\r'; out[1] = '\n';
        SendMessage(hed, SCI_APPENDTEXT, 2, (LPARAM)out);
    }

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

    char*    buf = (char*)malloc(IO_CHUNK + 8);
    wchar_t* w   = (wchar_t*)malloc((IO_CHUNK + 8) * sizeof(wchar_t));
    wchar_t* wn  = (wchar_t*)malloc((2 * IO_CHUNK + 8) * sizeof(wchar_t));
    char*    out = (char*)malloc(4 * IO_CHUNK + 16);
    BOOL ok = FALSE, pendCR = FALSE;
    Sci_Position total = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    Sci_Position pos = 0;

    if (!buf || !w || !wn || !out) goto cleanup;

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
            while (take > 0 && buf[take-1] != '\n') take--;
            if (take == 0) {
                take = (total - pos < IO_CHUNK) ? total - pos : IO_CHUNK;
                while (take > 0 && ((unsigned char)buf[take-1] & 0xC0) == 0x80) take--;
                if (take > 0 && (unsigned char)buf[take-1] >= 0xC0) take--;
            }
        }
        pos += take;

        /* 编辑器内部文档即 UTF-8 + CRLF：目标为 UTF-8 且 CRLF 时，
           若本块已是纯 CRLF（无孤立 \r 或 \n），直接原样落盘，
           跳过 UTF-8→UTF-16→EOL 规范化→UTF-8 的三重转换 */
        if ((enc == ENC_UTF8 || enc == ENC_UTF8_BOM) && eol == 0 && take > 0) {
            BOOL pure = TRUE;
            for (Sci_Position i = 0; i < take; i++) {
                if (buf[i] == '\n' && (i == 0 || buf[i-1] != '\r')) { pure = FALSE; break; }
                if (buf[i] == '\r' && (i + 1 >= take || buf[i+1] != '\n')) { pure = FALSE; break; }
            }
            if (pure) {
                DWORD wr = 0;
                if (!WriteFile(h, buf, (DWORD)take, &wr, NULL) || wr != (DWORD)take) goto cleanup;
                if (progress && !progress((UINT64)pos, (UINT64)total, ctx)) goto cleanup;
                continue;
            }
        }

        int n = 0;
        if (take > 0) n = MultiByteToWideChar(CP_UTF8, 0, buf, (int)take, w, IO_CHUNK + 8);
        if (take > 0 && n <= 0) goto cleanup;

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
        } else {
            int m = WideCharToMultiByte(CP_ACP, 0, wn, (int)wn2, out, (int)(4 * IO_CHUNK), "?", NULL);
            if (wn2 > 0 && m <= 0) goto cleanup;
            outLen = (DWORD)m;
        }
        if (outLen) {
            DWORD wr = 0;
            if (!WriteFile(h, out, outLen, &wr, NULL) || wr != outLen) goto cleanup;
        }
        if (progress && !progress((UINT64)pos, (UINT64)total, ctx)) goto cleanup;
    }

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
