#include "common.h"

#define SB_PARTS 5

static wchar_t s_text[SB_PARTS][64];
static int s_partR[SB_PARTS];
static BOOL s_haveParts = FALSE;

static HFONT s_font = NULL;
static UINT s_fontDpi = 0;

static HFONT StatusFont(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (s_font && dpi == s_fontDpi) return s_font;
    if (s_font) DeleteObject(s_font);
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
            if (s_text[i][0])
                DrawTextW(hdc, s_text[i], -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
