/* devtools.c — 开发者工具 UI 层：选区/文档编解码、哈希对话框、UUID、时间戳。
   算法实现在 devtutil.c（纯函数，独立自测）。 */

#include "common.h"
#include <bcrypt.h>

/* ---------------- 取选区/全文 ---------------- */

/* 选区优先，否则全文；返回 malloc 的 UTF-8 缓冲（调用方 free）。
   返回 NULL 表示无活动文档或内存不足。 */
static char* Dt_GetRange(size_t* pLen, Sci_Position* pStart, Sci_Position* pEnd,
                         BOOL* pIsSel) {
    *pLen = 0; *pStart = 0; *pEnd = 0;
    if (pIsSel) *pIsSel = FALSE;
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return NULL;
    HWND hed = g_docs[g_curDoc].hwndEdit;
    if (!hed) return NULL;
    Sci_Position docLen = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    Sci_Position s = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position e = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONEND, 0, 0);
    BOOL sel = (e > s);
    if (!sel) { s = 0; e = docLen; }
    *pStart = s; *pEnd = e;
    if (pIsSel) *pIsSel = sel;
    size_t len = (size_t)(e - s);
    char* buf = (char*)malloc(len + 1);
    if (!buf) return NULL;
    if (len) {
        struct Sci_TextRange tr;
        tr.chrg.cpMin = (Sci_PositionCR)s;
        tr.chrg.cpMax = (Sci_PositionCR)e;
        tr.lpstrText = buf;
        SendMessage(hed, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    } else {
        buf[0] = '\0';
    }
    *pLen = len;
    return buf;
}

/* ---------------- 编解码：直接替换选区/全文（同 JSON 工具交互） ---------------- */

void DevTools_TransformActiveDoc(int op) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    HWND hed = g_docs[g_curDoc].hwndEdit;
    if (!hed) return;

    size_t len = 0;
    Sci_Position start = 0, end = 0;
    char* src = Dt_GetRange(&len, &start, &end, NULL);
    if (!src) return;

    if (len == 0) {
        MessageBoxW(g_hwndMain, T(STR_DT_EMPTY), T(STR_APP_TITLE), MB_ICONINFORMATION);
        free(src);
        return;
    }
    if (len > (size_t)(100 * 1024 * 1024)) {
        wchar_t msg[128];
        WFmt(msg, T(STR_MSG_DT_BIG), 100);
        MessageBoxW(g_hwndMain, msg, T(STR_APP_TITLE), MB_ICONINFORMATION);
        free(src);
        return;
    }

    char* out = NULL;
    size_t outLen = 0, errPos = 0;
    BOOL ok = FALSE;
    switch (op) {
        case DT_B64_ENC:   out = DevUtil_B64Encode(src, len, &outLen);  ok = (out != NULL); break;
        case DT_URL_ENC:   out = DevUtil_UrlEncode(src, len, &outLen);  ok = (out != NULL); break;
        case DT_UNI_ESC:   out = DevUtil_Escape(src, len, &outLen);     ok = (out != NULL); break;
        case DT_B64_DEC:   ok = DevUtil_B64Decode(src, len, &out, &outLen, &errPos);   break;
        case DT_URL_DEC:   ok = DevUtil_UrlDecode(src, len, &out, &outLen, &errPos);   break;
        case DT_UNI_UNESC: ok = DevUtil_Unescape(src, len, &out, &outLen, &errPos);    break;
        default: free(src); return;
    }
    free(src);

    if (!ok) {
        Sci_Position abs = start + (Sci_Position)errPos;
        if (abs > end) abs = end;
        wchar_t msg[160];
        WFmt(msg, T(STR_DT_ERR_FMT), (int)errPos + 1);
        MessageBoxW(g_hwndMain, msg, T(STR_APP_TITLE), MB_ICONERROR);
        SendMessage(hed, SCI_GOTOPOS, abs, 0);
        SendMessage(hed, SCI_SETSEL, abs, (abs < end) ? abs + 1 : abs);
        SendMessage(hed, SCI_SCROLLCARET, 0, 0);
        return;
    }
    if (!out) {   /* 编码内存不足 */
        MessageBoxW(g_hwndMain, T(STR_JERR_MEM), T(STR_ERROR), MB_ICONERROR);
        return;
    }

    SendMessage(hed, SCI_BEGINUNDOACTION, 0, 0);
    SendMessage(hed, SCI_SETTARGETSTART, start, 0);
    SendMessage(hed, SCI_SETTARGETEND, end, 0);
    SendMessage(hed, SCI_REPLACETARGET, (WPARAM)outLen, (LPARAM)out);
    SendMessage(hed, SCI_ENDUNDOACTION, 0, 0);
    SendMessage(hed, SCI_SETSEL, start, start);
    SendMessage(hed, SCI_SCROLLCARET, 0, 0);
    free(out);
}

/* ---------------- UUID ---------------- */

void DevTools_InsertUuid(void) {
    char uuid[37];
    if (!DevUtil_UuidV4(uuid)) {
        MessageBoxW(g_hwndMain, T(STR_ERROR), T(STR_ERROR), MB_ICONERROR);
        return;
    }
    HWND hed = Editor_ActiveEdit();
    if (!hed) return;
    Sci_Position s = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position e = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONEND, 0, 0);
    SendMessage(hed, SCI_BEGINUNDOACTION, 0, 0);
    SendMessage(hed, SCI_SETTARGETSTART, s, 0);
    SendMessage(hed, SCI_SETTARGETEND, e, 0);
    SendMessage(hed, SCI_REPLACETARGET, 36, (LPARAM)uuid);
    SendMessage(hed, SCI_ENDUNDOACTION, 0, 0);
    Sci_Position caret = s + 36;
    SendMessage(hed, SCI_SETSEL, caret, caret);
    SendMessage(hed, SCI_SCROLLCARET, 0, 0);
}

/* ---------------- 剪贴板 ---------------- */

static BOOL Dt_CopyText(HWND owner, const wchar_t* text) {
    if (!text || !*text) return FALSE;
    if (!OpenClipboard(owner)) return FALSE;
    EmptyClipboard();
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    BOOL ok = FALSE;
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) {
            memcpy(p, text, bytes);
            GlobalUnlock(h);
            ok = (SetClipboardData(CF_UNICODETEXT, h) != NULL);
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

/* ---------------- 哈希对话框 ---------------- */

static const wchar_t* const kHashAlgs[4] = {
    BCRYPT_MD5_ALGORITHM, BCRYPT_SHA1_ALGORITHM,
    BCRYPT_SHA256_ALGORITHM, BCRYPT_SHA512_ALGORITHM
};
static const DWORD kHashLens[4] = { 16, 20, 32, 64 };
static const int kHashEdits[4] = { IDC_HASH_MD5, IDC_HASH_SHA1, IDC_HASH_SHA256, IDC_HASH_SHA512 };

typedef struct {
    BYTE md5[16], sha1[20], sha256[32], sha512[64];
    BYTE* digs[4];
    BOOL ok[4];
} DtHashData;

static void ToHexW(const BYTE* d, DWORD n, BOOL upper, wchar_t* out) {
    const wchar_t* hx = upper ? L"0123456789ABCDEF" : L"0123456789abcdef";
    for (DWORD i = 0; i < n; i++) {
        out[i * 2]     = hx[d[i] >> 4];
        out[i * 2 + 1] = hx[d[i] & 0x0F];
    }
    out[n * 2] = L'\0';
}

static void Hash_Render(HWND hdlg, const DtHashData* hd, BOOL upper) {
    wchar_t hex[129];
    for (int i = 0; i < 4; i++) {
        if (!hd->ok[i]) { SetDlgItemTextW(hdlg, kHashEdits[i], T(STR_ERROR)); continue; }
        ToHexW(hd->digs[i], kHashLens[i], upper, hex);
        SetDlgItemTextW(hdlg, kHashEdits[i], hex);
    }
}

static INT_PTR CALLBACK HashDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static DtHashData s_hd;
    if (msg == WM_INITDIALOG) {
        I18n_ApplyDialog(hdlg, IDD_HASH);
        size_t len = 0;
        Sci_Position start = 0, end = 0;
        BOOL isSel = FALSE;
        char* src = Dt_GetRange(&len, &start, &end, &isSel);
        if (!src) { EndDialog(hdlg, 0); return TRUE; }
        if (len > (size_t)(100 * 1024 * 1024)) {
            wchar_t msg2[128];
            WFmt(msg2, T(STR_MSG_DT_BIG), 100);
            MessageBoxW(g_hwndMain, msg2, T(STR_APP_TITLE), MB_ICONINFORMATION);
            free(src);
            EndDialog(hdlg, 0);
            return TRUE;
        }
        memset(&s_hd, 0, sizeof(s_hd));
        s_hd.digs[0] = s_hd.md5;
        s_hd.digs[1] = s_hd.sha1;
        s_hd.digs[2] = s_hd.sha256;
        s_hd.digs[3] = s_hd.sha512;
        for (int i = 0; i < 4; i++)
            s_hd.ok[i] = DevUtil_Hash(kHashAlgs[i], (const BYTE*)src, (DWORD)len,
                                      s_hd.digs[i], kHashLens[i]);
        free(src);
        wchar_t lbl[96];
        WFmt(lbl, T(isSel ? STR_HASH_SEL : STR_HASH_ALL), (int)len);
        SetDlgItemTextW(hdlg, IDC_HASH_SRC, lbl);
        CheckDlgButton(hdlg, IDC_HASH_UPPER, BST_UNCHECKED);
        Hash_Render(hdlg, &s_hd, FALSE);
        SetFocus(GetDlgItem(hdlg, IDC_HASH_CLOSE));
        return FALSE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_HASH_CLOSE || id == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
        if (id == IDC_HASH_UPPER) {
            Hash_Render(hdlg, &s_hd, IsDlgButtonChecked(hdlg, IDC_HASH_UPPER) == BST_CHECKED);
            return TRUE;
        }
        if (id >= IDC_HASH_C0 && id <= IDC_HASH_C3) {
            int row = id - IDC_HASH_C0;
            wchar_t hex[129];
            GetDlgItemTextW(hdlg, kHashEdits[row], hex, 129);
            Dt_CopyText(hdlg, hex);
            return TRUE;
        }
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}

void DevTools_ShowHashDlg(HWND parent) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_HASH), parent, HashDlgProc, 0);
}

/* ---------------- 时间戳转换对话框 ---------------- */

static const wchar_t* TsNum(const wchar_t* p, int* v, int maxDigits) {
    if (*p < L'0' || *p > L'9') return NULL;
    int acc = 0, k = 0;
    while (*p >= L'0' && *p <= L'9' && k < maxDigits) {
        acc = acc * 10 + (*p - L'0');
        p++; k++;
    }
    *v = acc;
    return p;
}

/* 解析 YYYY-MM-DD[ HH:MM[:SS]]（'/' 与 'T' 分隔符兼容，月/日/时/分/秒可单个数字）；
   输入按本地时间解释。合法性由 SystemTimeToFileTime 兜底校验。 */
static BOOL TsParseDateTime(const wchar_t* s, SYSTEMTIME* st) {
    wchar_t buf[64];
    if (wcslen(s) >= 64) return FALSE;
    wcscpy(buf, s);
    for (wchar_t* p = buf; *p; p++) {
        if (*p == L'/') *p = L'-';
        else if (*p == L'T') *p = L' ';
    }
    const wchar_t* p = buf;
    int Y, M, D, h = 0, m = 0, sec = 0;
    if (!(p = TsNum(p, &Y, 4)) || *p != L'-') return FALSE;
    p++;
    if (!(p = TsNum(p, &M, 2)) || *p != L'-') return FALSE;
    p++;
    if (!(p = TsNum(p, &D, 2))) return FALSE;
    if (*p == L' ') {
        p++;
        if (!(p = TsNum(p, &h, 2)) || *p != L':') return FALSE;
        p++;
        if (!(p = TsNum(p, &m, 2))) return FALSE;
        if (*p == L':') {
            p++;
            if (!(p = TsNum(p, &sec, 2))) return FALSE;
        }
    }
    if (*p) return FALSE;
    if (Y < 1601 || Y > 30827 || M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59)
        return FALSE;
    memset(st, 0, sizeof(*st));
    st->wYear = (WORD)Y;
    st->wMonth = (WORD)M;
    st->wDay = (WORD)D;
    st->wHour = (WORD)h;
    st->wMinute = (WORD)m;
    st->wSecond = (WORD)sec;
    FILETIME ft;
    return SystemTimeToFileTime(st, &ft) != 0;
}

static void TsAppendLine(wchar_t* out, int cch, const wchar_t* line) {
    int l = (int)wcslen(out);
    if (l >= cch - 3) return;
    _snwprintf(out + l, cch - l, L"%s\r\n", line);
    out[cch - 1] = L'\0';
}

static void TsFillNow(HWND hdlg) {
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    ULARGE_INTEGER u;
    u.LowPart = f.dwLowDateTime;
    u.HighPart = f.dwHighDateTime;
    long long sec = (long long)(u.QuadPart / 10000000ULL - 11644473600ULL);
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%lld", sec);
    buf[31] = L'\0';
    SetDlgItemTextW(hdlg, IDC_TS_IN, buf);
}

static void TsConvert(HWND hdlg) {
    wchar_t in[128];
    GetDlgItemTextW(hdlg, IDC_TS_IN, in, 128);
    wchar_t* s = in;
    while (*s == L' ' || *s == L'\t') s++;
    wchar_t* e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n')) e--;
    *e = L'\0';
    if (!*s) {
        SetDlgItemTextW(hdlg, IDC_TS_OUT, T(STR_TS_ERR));
        return;
    }

    BOOL numeric = TRUE;
    const wchar_t* p = s;
    if (*p == L'-' || *p == L'+') p++;
    if (!*p) numeric = FALSE;
    for (; *p; p++)
        if (*p < L'0' || *p > L'9') { numeric = FALSE; break; }

    wchar_t out[640];
    out[0] = L'\0';
    wchar_t line[160];

    if (numeric) {
        long long v = _wcstoi64(s, NULL, 10);
        BOOL isMs = (v >= 100000000000LL || v <= -100000000000LL);
        long long sec = isMs ? v / 1000 : v;
        long long ftv = sec * 10000000LL + 116444736000000000LL;
        if (ftv < 0) {
            SetDlgItemTextW(hdlg, IDC_TS_OUT, T(STR_TS_ERR));
            return;
        }
        ULARGE_INTEGER uu;
        uu.QuadPart = (ULONGLONG)ftv;
        FILETIME f;
        f.dwLowDateTime = uu.LowPart;
        f.dwHighDateTime = uu.HighPart;
        SYSTEMTIME utc, loc;
        if (!FileTimeToSystemTime(&f, &utc)) {
            SetDlgItemTextW(hdlg, IDC_TS_OUT, T(STR_TS_ERR));
            return;
        }
        SystemTimeToTzSpecificLocalTime(NULL, &utc, &loc);
        WFmt(line, T(STR_TS_OUT_SEC), sec);
        TsAppendLine(out, 640, line);
        if (isMs) {
            WFmt(line, T(STR_TS_OUT_MS), v);
            TsAppendLine(out, 640, line);
        }
        WFmt(line, T(STR_TS_OUT_LOCAL),
             loc.wYear, loc.wMonth, loc.wDay, loc.wHour, loc.wMinute, loc.wSecond);
        TsAppendLine(out, 640, line);
        WFmt(line, T(STR_TS_OUT_UTC),
             utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
        TsAppendLine(out, 640, line);
    } else {
        SYSTEMTIME st, utc;
        if (!TsParseDateTime(s, &st) || !TzSpecificLocalTimeToSystemTime(NULL, &st, &utc)) {
            SetDlgItemTextW(hdlg, IDC_TS_OUT, T(STR_TS_ERR));
            return;
        }
        FILETIME f;
        SystemTimeToFileTime(&utc, &f);
        ULARGE_INTEGER u;
        u.LowPart = f.dwLowDateTime;
        u.HighPart = f.dwHighDateTime;
        long long sec = (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
        WFmt(line, T(STR_TS_OUT_SEC), sec);
        TsAppendLine(out, 640, line);
        WFmt(line, T(STR_TS_OUT_MS), sec * 1000);
        TsAppendLine(out, 640, line);
        WFmt(line, T(STR_TS_OUT_LOCAL),
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        TsAppendLine(out, 640, line);
        WFmt(line, T(STR_TS_OUT_UTC),
             utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
        TsAppendLine(out, 640, line);
    }
    SetDlgItemTextW(hdlg, IDC_TS_OUT, out);
}

static INT_PTR CALLBACK TsDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        I18n_ApplyDialog(hdlg, IDD_TS);
        TsFillNow(hdlg);       /* 默认带入当前时间戳，直接可对照转换 */
        TsConvert(hdlg);
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_TS_CLOSE || id == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
        if (id == IDC_TS_GO) { TsConvert(hdlg); return TRUE; }
        if (id == IDC_TS_NOW) { TsFillNow(hdlg); TsConvert(hdlg); return TRUE; }
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}

void DevTools_ShowTsDlg(HWND parent) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_TS), parent, TsDlgProc, 0);
}
