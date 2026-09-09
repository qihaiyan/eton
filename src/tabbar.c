#include "common.h"

/* ---------- 自绘标签栏（窗口类 "NPPTabBar"） ----------
   由 main.c 创建窗口；点击标签发 WM_APP_TABSEL、点 × / 中键发 WM_APP_TABCLOSE、
   双击空白发 IDM_NEW 给主窗口处理。标签数据（标题/数量/激活）直接读全局 g_docs。
   标签放不下时右侧显示 ◀▶ 滚动按钮（Marlett 字体字符 3/4）；
   悬停高亮标签与 × ，并用 tooltip 显示完整路径。 */

static const int ARROW_W_96 = 16;    /* 滚动按钮宽（96-DPI 基准） */
#define MTBH_SCROLL_STEP UI_Scale(80)

static int  s_scroll = 0;            /* 水平滚动截去的像素 */
static int  s_hoverTab = -1;         /* 悬停标签索引 */
static BOOL s_hoverClose = FALSE;    /* 悬停在 × 上 */
static int  s_hoverArrow = -1;       /* 悬停滚动按钮 0/1 */
static BOOL s_tracking = FALSE;      /* TrackMouseEvent 已登记 */
static HWND s_tip = NULL;            /* tooltip 控件 */

/* tooltip 深/浅主题跟随（DarkMode_Explorer 为系统主题串，失败则保持默认） */
void TabBar_ApplyTheme(void) {
    if (!s_tip) return;
    typedef BOOL (WINAPI *SetThemeFn)(HWND, LPCWSTR, LPCWSTR);
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    SetThemeFn st = (SetThemeFn)(void*)GetProcAddress(ux, "SetWindowTheme");
    if (st) st(s_tip, g_dark ? L"DarkMode_Explorer" : L"", NULL);
    FreeLibrary(ux);
}
static wchar_t s_tipText[1024];      /* tooltip 文本缓冲（控件引用其指针） */
static int  s_tipTab = -1;

/* 计算每个标签的矩形（未滚动坐标系：x 从 0 起累加） */
static void TabBar_Rects(HWND hwnd, RECT* out, int* n) {
    RECT rc; GetClientRect(hwnd, &rc);
    int count = g_docCount;
    int x = 0;
    for (int i = 0; i < count && i < MAX_DOCS; i++) {
        HDC hdc = GetDC(hwnd);
        HFONT old = (HFONT)SelectObject(hdc, g_hFont);
        SIZE sz; GetTextExtentPoint32W(hdc, g_docs[i].title, (int)wcslen(g_docs[i].title), &sz);
        SelectObject(hdc, old); ReleaseDC(hwnd, hdc);
        int w = sz.cx + UI_Scale(40);
        if (w < UI_Scale(90)) w = UI_Scale(90);
        if (w > UI_Scale(240)) w = UI_Scale(240);
        out[i].left = x; out[i].top = 0;
        out[i].right = x + w; out[i].bottom = rc.bottom;
        x += w;
    }
    *n = count;
}

static int ArrowW(void) { return UI_Scale(ARROW_W_96); }

/* 标签总宽是否超出可视区（决定是否显示滚动按钮） */
static BOOL TabBar_Overflow(HWND hwnd, int totalW) {
    RECT rc; GetClientRect(hwnd, &rc);
    return totalW > rc.right;
}

static void TabBar_ClampScroll(int totalW, int visible) {
    int maxScroll = totalW - visible;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scroll > maxScroll) s_scroll = maxScroll;
    if (s_scroll < 0) s_scroll = 0;
}

/* 命中测试：返回标签索引（-1 无），closeHit=点在 × 上；arrow=-1/0/1 */
static int TabBar_HitTest(HWND hwnd, int mx, int my, BOOL* closeHit, int* arrow) {
    *closeHit = FALSE;
    if (arrow) *arrow = -1;
    RECT rc; GetClientRect(hwnd, &rc);
    if (mx < 0 || mx >= rc.right || my < 0 || my >= rc.bottom) return -1;
    RECT rects[MAX_DOCS]; int n = 0;
    TabBar_Rects(hwnd, rects, &n);
    int totalW = n ? rects[n - 1].right : 0;
    BOOL ovf = TabBar_Overflow(hwnd, totalW);
    if (ovf && mx >= rc.right - 2 * ArrowW()) {
        if (arrow) *arrow = (mx >= rc.right - ArrowW()) ? 1 : 0;
        return -1;
    }
    int sx = mx + s_scroll;
    for (int i = 0; i < n; i++) {
        if (sx >= rects[i].left && sx < rects[i].right) {
            int cbL = rects[i].right - UI_Scale(16);
            int cbT = (rects[i].bottom - rects[i].top) / 2 - UI_Scale(7);
            int cbB = (rects[i].bottom - rects[i].top) / 2 + UI_Scale(7);
            if (sx >= cbL && my >= cbT && my <= cbB) *closeHit = TRUE;
            return i;
        }
    }
    return -1;
}

/* 更新 tooltip 文本（唯一 tool，uId=0） */
static void TabBar_SetTipText(HWND hwnd, int tab) {
    if (!s_tip) return;
    if (tab == s_tipTab) return;
    s_tipTab = tab;
    if (tab >= 0 && tab < g_docCount) {
        Doc* d = &g_docs[tab];
        const wchar_t* t = (!d->isNew && d->path[0]) ? d->path : d->baseTitle;
        _snwprintf(s_tipText, 1024, L"%s", t);
        s_tipText[1023] = L'\0';
    } else {
        s_tipText[0] = L'\0';
    }
    TOOLINFO ti; memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uId = 0;
    ti.lpszText = s_tipText;
    SendMessage(s_tip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

static LRESULT CALLBACK TabBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        s_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                WS_POPUP | TTS_NOPREFIX, 0, 0, 0, 0,
                                hwnd, NULL, g_hInst, NULL);
        if (s_tip) {
            TOOLINFO ti; memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = hwnd;
            ti.uId = 0;
            GetClientRect(hwnd, &ti.rect);
            ti.lpszText = s_tipText;
            s_tipText[0] = L'\0';
            SendMessage(s_tip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            TabBar_ApplyTheme();
        }
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_dark ? RGB(37,37,38) : RGB(225,225,228));
        FillRect(hdc, &rc, bg); DeleteObject(bg);

        RECT rects[MAX_DOCS]; int n = 0;
        TabBar_Rects(hwnd, rects, &n);
        int totalW = n ? rects[n - 1].right : 0;
        BOOL ovf = TabBar_Overflow(hwnd, totalW);
        int aw = ArrowW();
        int visible = ovf ? (rc.right - 2 * aw) : rc.right;
        TabBar_ClampScroll(totalW, visible);

        /* 保证激活标签可见 */
        if (g_curDoc >= 0 && g_curDoc < n && n > 0) {
            if (rects[g_curDoc].left < s_scroll)
                s_scroll = rects[g_curDoc].left;
            if (rects[g_curDoc].right - s_scroll > visible)
                s_scroll = rects[g_curDoc].right - visible;
            TabBar_ClampScroll(totalW, visible);
        }

        /* 仅在可视区内绘制标签 */
        if (n > 0) {
            HRGN clip = CreateRectRgn(0, 0, visible, rc.bottom);
            SelectClipRgn(hdc, clip);
            for (int i = 0; i < n; i++) {
                RECT r = rects[i];
                r.left -= s_scroll; r.right -= s_scroll;
                if (r.right < 0 || r.left > visible) continue;
                BOOL active = (i == g_curDoc);
                BOOL hovered = (i == s_hoverTab);
                HBRUSH tb = CreateSolidBrush(
                    active ? (g_dark ? RGB(60,60,62)    : RGB(255,255,255))
                           : hovered ? (g_dark ? RGB(52,52,55) : RGB(243,243,245))
                                     : (g_dark ? RGB(45,45,46) : RGB(235,235,238)));
                FillRect(hdc, &r, tb); DeleteObject(tb);
                if (active) {
                    RECT top = {r.left, r.top, r.right, r.top + 2};
                    HBRUSH hl = CreateSolidBrush(RGB(0,120,215));
                    FillRect(hdc, &top, hl); DeleteObject(hl);
                }
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, g_dark ? RGB(220,220,220) : RGB(0,0,0));
                HFONT old = (HFONT)SelectObject(hdc, g_hFont);
                RECT tr = {r.left + UI_Scale(8), r.top, r.right - UI_Scale(18), r.bottom};
                DrawTextW(hdc, g_docs[i].title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                SelectObject(hdc, old);
                /* 关闭按钮 ×（悬停时加底色） */
                RECT cb = {r.right - UI_Scale(16), r.top + (r.bottom - r.top)/2 - UI_Scale(7),
                           r.right - UI_Scale(4),  r.top + (r.bottom - r.top)/2 + UI_Scale(7)};
                if (hovered && s_hoverClose) {
                    HBRUSH hb = CreateSolidBrush(g_dark ? RGB(80,80,84) : RGB(210,210,214));
                    FillRect(hdc, &cb, hb); DeleteObject(hb);
                }
                SetTextColor(hdc, active ? (g_dark ? RGB(220,220,220) : RGB(0,0,0))
                                         : (g_dark ? RGB(180,180,180) : RGB(90,90,90)));
                wchar_t xc[2] = {L'x', 0};
                DrawTextW(hdc, xc, 1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectClipRgn(hdc, NULL);
            DeleteObject(clip);
        }

        /* 滚动按钮（Marlett 字符 3=◀ 4=▶） */
        if (ovf) {
            HFONT mar = CreateFontW(-UI_Scale(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Marlett");
            HFONT old = (HFONT)SelectObject(hdc, mar);
            for (int a = 0; a < 2; a++) {
                RECT ab = {rc.right - 2*aw + a*aw, 0, rc.right - aw + a*aw, rc.bottom};
                BOOL on = (a == 0) ? (s_scroll > 0) : (s_scroll < totalW - visible);
                HBRUSH abg = CreateSolidBrush(
                    a == s_hoverArrow ? (g_dark ? RGB(70,70,74) : RGB(200,200,205))
                                      : (g_dark ? RGB(45,45,48) : RGB(215,215,218)));
                FillRect(hdc, &ab, abg); DeleteObject(abg);
                SetTextColor(hdc, on ? (g_dark ? RGB(220,220,220) : RGB(60,60,60))
                                     : (g_dark ? RGB(100,100,100) : RGB(160,160,160)));
                wchar_t ch[2] = { a == 0 ? L'3' : L'4', 0 };
                DrawTextW(hdc, ch, 1, &ab, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(hdc, old);
            DeleteObject(mar);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_LBUTTONDOWN) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        BOOL closeHit; int arrow;
        TabBar_HitTest(hwnd, mx, my, &closeHit, &arrow);
        if (arrow == 0) { s_scroll -= MTBH_SCROLL_STEP; InvalidateRect(hwnd, NULL, TRUE); return 0; }
        if (arrow == 1) { s_scroll += MTBH_SCROLL_STEP; InvalidateRect(hwnd, NULL, TRUE); return 0; }
        int i = TabBar_HitTest(hwnd, mx, my, &closeHit, NULL);
        if (i >= 0) {
            if (closeHit) PostMessage(g_hwndMain, WM_APP_TABCLOSE, (WPARAM)i, 0);
            else PostMessage(g_hwndMain, WM_APP_TABSEL, (WPARAM)i, 0);
        }
        return 0;
    }
    if (msg == WM_LBUTTONDBLCLK) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        BOOL closeHit; int arrow;
        int i = TabBar_HitTest(hwnd, mx, my, &closeHit, &arrow);
        if (i < 0 && arrow < 0)
            PostMessage(g_hwndMain, WM_COMMAND, MAKEWPARAM(IDM_NEW, 0), 0);   /* 双击空白新建 */
        return 0;
    }
    if (msg == WM_MBUTTONDOWN) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        BOOL closeHit; int arrow;
        int i = TabBar_HitTest(hwnd, mx, my, &closeHit, &arrow);
        if (i >= 0) PostMessage(g_hwndMain, WM_APP_TABCLOSE, (WPARAM)i, 0);   /* 中键关闭 */
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        RECT rects[MAX_DOCS]; int n = 0;
        TabBar_Rects(hwnd, rects, &n);
        int totalW = n ? rects[n - 1].right : 0;
        RECT rc; GetClientRect(hwnd, &rc);
        BOOL ovf = TabBar_Overflow(hwnd, totalW);
        if (ovf) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            s_scroll -= (delta / WHEEL_DELTA) * UI_Scale(60);
            TabBar_ClampScroll(totalW, rc.right - 2 * ArrowW());
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (s_tip) { MSG m; m.hwnd = hwnd; m.message = WM_MOUSEMOVE; m.wParam = 0; m.lParam = lp;
                     m.pt.x = mx; m.pt.y = my; SendMessage(s_tip, TTM_RELAYEVENT, 0, (LPARAM)&m); }
        BOOL closeHit; int arrow;
        int i = TabBar_HitTest(hwnd, mx, my, &closeHit, &arrow);
        if (i != s_hoverTab || closeHit != s_hoverClose || arrow != s_hoverArrow) {
            s_hoverTab = i; s_hoverClose = closeHit; s_hoverArrow = arrow;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TabBar_SetTipText(hwnd, i);
        if (!s_tracking) {
            TRACKMOUSEEVENT tme; memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_tracking = TRUE;
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        s_tracking = FALSE;
        if (s_hoverTab != -1 || s_hoverClose || s_hoverArrow != -1) {
            s_hoverTab = -1; s_hoverClose = FALSE; s_hoverArrow = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TabBar_Register(void) {
    WNDCLASSEXW tc; memset(&tc, 0, sizeof(tc));
    tc.cbSize = sizeof(tc);
    tc.style = CS_DBLCLKS;
    tc.lpfnWndProc = TabBarProc;
    tc.hInstance = g_hInst;
    tc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    tc.lpszClassName = L"NPPTabBar";
    tc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&tc);
}

/* main.c 右键菜单用的命中测试封装（详见 TabBar_HitTest） */
int TabBar_HitTestPublic(HWND hwnd, int mx, int my, BOOL* closeHit, int* arrow) {
    return TabBar_HitTest(hwnd, mx, my, closeHit, arrow);
}
