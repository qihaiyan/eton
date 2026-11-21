#include "common.h"
#define SB_PARTS 6
#define SB_PART_ENC 3
#define SB_PART_LANG 4
#define SB_PART_EOL 5
static wchar_t s_text[SB_PARTS][64];
static int s_partR[SB_PARTS];
static BOOL s_haveParts = FALSE;
static HFONT s_font = NULL;
static HFONT s_fontU = NULL;
static UINT s_fontDpi = 0;
static int s_hoverPart = -1;
static BOOL s_hoverTracking = FALSE;
static HFONT StatusFont(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (s_font && dpi == s_fontDpi) return s_font;
    if (s_font) DeleteObject(s_font);
    if (s_fontU) { DeleteObject(s_fontU); s_fontU = NULL; }
    NONCLIENTMETRICSW ncm; memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    UINT sysDpi = GetDpiForSystem();
    if (dpi != sysDpi)
        ncm.lfStatusFont.lfHeight = -MulDiv(-ncm.lfStatusFont.lfHeight, (int)dpi, (int)sysDpi);
    s_font = CreateFontIndirectW(&ncm.lfStatusFont);
    s_fontDpi = dpi;
    return s_font;
}
static HFONT StatusFontUnder(HWND hwnd) {
    StatusFont(hwnd);
    if (!s_fontU) {
        LOGFONTW lf; GetObjectW(s_font, sizeof(lf), &lf);
        lf.lfUnderline = TRUE;
        s_fontU = CreateFontIndirectW(&lf);
    }
    return s_fontU;
}
static const wchar_t* EolSep(int i) {
    const wchar_t* t = s_text[i];
    const wchar_t* sep = wcsstr(t, L"  |  ");
    return (sep && sep != t) ? sep : NULL;
}
static BOOL TextZone(HWND hwnd, int i, int x, const wchar_t** sepOut) {
    if (!s_text[i][0]) return FALSE;
    const wchar_t* sep = NULL;
    int wlen = (int)wcslen(s_text[i]);
    if (i == SB_PART_EOL) {
        sep = EolSep(i);
        if (!sep) return FALSE;
        wlen = (int)(sep - s_text[i]);
    }
    if (sepOut) *sepOut = sep;
    int l = (i == 0) ? 0 : s_partR[i - 1];
    if (x < l + 8) return FALSE;
    HDC hdc = GetDC(hwnd);
    HFONT old = (HFONT)SelectObject(hdc, StatusFont(hwnd));
    SIZE sz;
    GetTextExtentPoint32W(hdc, s_text[i], wlen, &sz);
    SelectObject(hdc, old);
    ReleaseDC(hwnd, hdc);
    return x <= l + 8 + sz.cx;
}
static int HoverPart(HWND hwnd, int x) {
    RECT rc; GetClientRect(hwnd, &rc);
    for (int i = SB_PART_EOL; i >= SB_PART_ENC; i--) {
        int l = (i == 0) ? 0 : s_partR[i - 1];
        int r = (i == SB_PARTS - 1 || s_partR[i] < 0) ? rc.right : s_partR[i];
        if (l < 0) l = 0;
        if (x >= l && x < r) return TextZone(hwnd, i, x, NULL) ? i : -1;
    }
    return -1;
}
static void ShowPartPopup(HWND hwnd, int part) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    HMENU m = CreatePopupMenu();
    if (part == SB_PART_EOL) {
        int eol = g_docs[g_curDoc].eol;
        I18n_OwnerAppend(m, MF_STRING | (eol == 0 ? MF_CHECKED : 0),
                         IDM_EOL_CRLF, T(STR_EOLNAME_CRLF));
        I18n_OwnerAppend(m, MF_STRING | (eol == 1 ? MF_CHECKED : 0),
                         IDM_EOL_LF, T(STR_EOLNAME_LF));
        I18n_OwnerAppend(m, MF_STRING | (eol == 2 ? MF_CHECKED : 0),
                         IDM_EOL_CR, T(STR_EOLNAME_CR));
    } else if (part == SB_PART_ENC) {
        int enc = g_docs[g_curDoc].enc;
        for (int e = ENC_ANSI; e <= ENC_UTF16BE; e++)
            I18n_OwnerAppend(m, MF_STRING | (enc == e ? MF_CHECKED : 0),
                             IDM_ENC_ANSI + e, EncodingName((Encoding)e));
    } else {
        for (int l = LANG_NONE; l <= LANG_YAML; l++)
            I18n_OwnerAppend(m, MF_STRING | (g_docs[g_curDoc].lang == l ? MF_CHECKED : 0),
                             IDM_LANG_NONE + l, LangDisplayName((LangID)l));
    }
    I18n_ApplyMenuTheme(m);
    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                             pt.x, pt.y, 0, g_hwndMain, NULL);
    DestroyMenu(m);
    if (cmd) PostMessage(g_hwndMain, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}
static LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_clrStatusBg);
        FillRect(hdc, &rc, bg); DeleteObject(bg);
        HFONT old = (HFONT)SelectObject(hdc, StatusFont(hwnd));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_clrStatusFg);
        int right = rc.right;
        for (int i = SB_PARTS - 1; i >= 0; i--) {
            int l = (i == 0) ? 0 : s_partR[i - 1];
            int r = (i == SB_PARTS - 1 || s_partR[i] < 0) ? right : s_partR[i];
            RECT tr = { l + 8, 0, r - 8, rc.bottom };
            if (s_text[i][0]) {
                const wchar_t* sep = (i == s_hoverPart && i == SB_PART_EOL) ? EolSep(i) : NULL;
                if (sep) {
                    SIZE w1;
                    GetTextExtentPoint32W(hdc, s_text[i], (int)(sep - s_text[i]), &w1);
                    RECT tr2 = tr; tr2.left = tr.left + w1.cx;
                    SelectObject(hdc, StatusFontUnder(hwnd));
                    DrawTextW(hdc, s_text[i], (int)(sep - s_text[i]), &tr,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(hdc, s_font);
                    DrawTextW(hdc, sep, -1, &tr2,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                } else if (i == s_hoverPart) {
                    SelectObject(hdc, StatusFontUnder(hwnd));
                    DrawTextW(hdc, s_text[i], -1, &tr,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SelectObject(hdc, s_font);
                } else {
                    DrawTextW(hdc, s_text[i], -1, &tr,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
            }
            if (i > 0) {
                COLORREF c = g_dark ? RGB(63,63,63) : RGB(160,160,160);
                RECT sep = { l, 2, l + 1, rc.bottom - 2 };
                HBRUSH sb = CreateSolidBrush(c);
                FillRect(hdc, &sep, sb); DeleteObject(sb);
            }
        }
        SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_MOUSEMOVE) {
        int x = GET_X_LPARAM(lp);
        int part = HoverPart(hwnd, x);
        if (part != s_hoverPart) {
            s_hoverPart = part;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (part >= 0 && !s_hoverTracking) {
            TRACKMOUSEEVENT tme; memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_hoverTracking = TRUE;
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        s_hoverTracking = FALSE;
        if (s_hoverPart >= 0) { s_hoverPart = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        if (HoverPart(hwnd, pt.x) >= 0) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
    }
    if (msg == WM_LBUTTONUP) {
        int x = GET_X_LPARAM(lp);
        int part = HoverPart(hwnd, x);
        if (part >= 0) {
            ShowPartPopup(hwnd, part);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void StatusBar_Register(void) {
    WNDCLASSEXW sc; memset(&sc, 0, sizeof(sc));
    sc.cbSize = sizeof(sc);
    sc.style = CS_HREDRAW | CS_VREDRAW;
    sc.lpfnWndProc = StatusProc;
    sc.hInstance = g_hInst;
    sc.lpszClassName = L"ETONStatus";
    sc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&sc);
}
void StatusBar_SetParts(const int* rightEdges, int n) {
    if (n != SB_PARTS) return;
    for (int i = 0; i < SB_PARTS; i++) s_partR[i] = rightEdges[i];
    s_haveParts = TRUE;
    InvalidateRect(g_hwndStatus, NULL, FALSE);
}
BOOL StatusBar_SetText(int part, const wchar_t* text) {
    if (part < 0 || part >= SB_PARTS || !text) return FALSE;
    if (wcscmp(s_text[part], text) == 0) return TRUE;
    _snwprintf(s_text[part], 64, L"%s", text);
    s_text[part][63] = L'\0';
    InvalidateRect(g_hwndStatus, NULL, FALSE);
    return TRUE;
}
