#include "common.h"

/* ---------- 自绘标签栏（窗口类 "NPPTabBar"） ----------
   由 main.c 创建窗口；点击标签发 WM_APP_TABSEL、点 × 发 WM_APP_TABCLOSE
   给主窗口处理。标签数据（标题/数量/激活）直接读全局 g_docs。 */

static void TabBar_Rects(HWND hwnd, RECT* out, int* n) {
    RECT rc; GetClientRect(hwnd, &rc);
    int count = g_docCount;
    int x = 0;
    for (int i = 0; i < count && i < MAX_DOCS; i++) {
        HDC hdc = GetDC(hwnd);
        HFONT old = (HFONT)SelectObject(hdc, g_hFont);
        SIZE sz; GetTextExtentPoint32W(hdc, g_docs[i].title, (int)wcslen(g_docs[i].title), &sz);
        SelectObject(hdc, old); ReleaseDC(hwnd, hdc);
        int w = sz.cx + 40;
        if (w < 90) w = 90;
        if (w > 240) w = 240;
        out[i].left = x; out[i].top = 0;
        out[i].right = x + w; out[i].bottom = rc.bottom;
        x += w;
    }
    *n = count;
}

static LRESULT CALLBACK TabBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_dark ? RGB(37,37,38) : RGB(225,225,228));
        FillRect(hdc, &rc, bg); DeleteObject(bg);
        RECT rects[MAX_DOCS]; int n = 0; TabBar_Rects(hwnd, rects, &n);
        for (int i = 0; i < n; i++) {
            BOOL active = (i == g_curDoc);
            HBRUSH tb = CreateSolidBrush(active ? (g_dark ? RGB(60,60,62) : RGB(255,255,255))
                                               : (g_dark ? RGB(45,45,46) : RGB(235,235,238)));
            FillRect(hdc, &rects[i], tb); DeleteObject(tb);
            if (active) {
                RECT top = {rects[i].left, rects[i].top, rects[i].right, rects[i].top + 2};
                HBRUSH hl = CreateSolidBrush(RGB(0,120,215));
                FillRect(hdc, &top, hl); DeleteObject(hl);
            }
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_dark ? RGB(220,220,220) : RGB(0,0,0));
            HFONT old = (HFONT)SelectObject(hdc, g_hFont);
            RECT tr = {rects[i].left + 8, rects[i].top, rects[i].right - 18, rects[i].bottom};
            DrawTextW(hdc, g_docs[i].title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, old);
            RECT cb = {rects[i].right - 16, rects[i].top + (rects[i].bottom - rects[i].top)/2 - 7,
                       rects[i].right - 4,  rects[i].top + (rects[i].bottom - rects[i].top)/2 + 7};
            wchar_t xc[2] = {L'x', 0};
            DrawTextW(hdc, xc, 1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_LBUTTONDOWN) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        RECT rects[MAX_DOCS]; int n = 0; TabBar_Rects(hwnd, rects, &n);
        for (int i = 0; i < n; i++) {
            if (mx >= rects[i].left && mx < rects[i].right) {
                RECT cb = {rects[i].right - 16, rects[i].top + 4,
                           rects[i].right - 4,  rects[i].bottom - 4};
                if (mx >= cb.left && mx <= cb.right && my >= cb.top && my <= cb.bottom) {
                    PostMessage(g_hwndMain, WM_APP_TABCLOSE, (WPARAM)i, 0);
                } else {
                    PostMessage(g_hwndMain, WM_APP_TABSEL, (WPARAM)i, 0);
                }
                return 0;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TabBar_Register(void) {
    WNDCLASSEXW tc; memset(&tc, 0, sizeof(tc));
    tc.cbSize = sizeof(tc);
    tc.lpfnWndProc = TabBarProc;
    tc.hInstance = g_hInst;
    tc.lpszClassName = L"NPPTabBar";
    tc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&tc);
}
