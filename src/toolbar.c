#include "common.h"

typedef struct { int cmd; int strId; int icon; } TbBtn;

static const TbBtn kBtns[] = {
    { IDM_NEW,     STR_ITEM_NEW,        0 },
    { IDM_OPEN,    STR_ITEM_OPEN,       1 },
    { IDM_SAVE,    STR_ITEM_SAVE,       2 },
    { 0,           0,                  -1 },
    { IDM_UNDO,    STR_ITEM_UNDO,       3 },
    { IDM_REDO,    STR_ITEM_REDO,       4 },
    { 0,           0,                  -1 },
    { IDM_CUT,     STR_ITEM_CUT,        5 },
    { IDM_COPY,    STR_ITEM_COPY,       6 },
    { IDM_PASTE,   STR_ITEM_PASTE,      7 },
    { 0,           0,                  -1 },
    { IDM_FIND,    STR_ITEM_FIND,       8 },
    { 0,           0,                  -1 },
    { IDM_ZOOMIN,  STR_ITEM_ZOOMIN,     9 },
    { IDM_ZOOMOUT, STR_ITEM_ZOOMOUT,   10 },
    { 0,           0,                  -1 },
    { IDM_WRAP,    STR_ITEM_WRAP,      11 },
    { IDM_THEME,   STR_ITEM_THEME,     12 },
    { IDM_VIEW_MD, STR_ITEM_MDPREVIEW, 13 },
};
#define BTN_N ((int)(sizeof(kBtns) / sizeof(kBtns[0])))

static int  s_hover = -1;
static int  s_pressBtn = -1;
static BOOL s_tracking = FALSE;
static unsigned s_stateHash = 1;
static HWND s_tip = NULL;
static HWND s_tipOwner = NULL;
static wchar_t s_tipText[128];
static int  s_tipBtn = -1;

static int BtnW(void) { return UI_Scale(30); }
static int SepW(void) { return UI_Scale(7); }
static int PadL(void) { return UI_Scale(2); }

static void TbRects(RECT rc, RECT* out) {
    int x = PadL();
    for (int i = 0; i < BTN_N; i++) {
        int w = (kBtns[i].icon < 0) ? SepW() : BtnW();
        out[i].left = x; out[i].right = x + w;
        out[i].top = 0; out[i].bottom = rc.bottom;
        x += w;
    }
}

static int HitBtn(HWND hwnd, int mx, int my) {
    RECT rc; GetClientRect(hwnd, &rc);
    if (mx < 0 || mx >= rc.right || my < 0 || my >= rc.bottom) return -1;
    RECT rects[BTN_N]; TbRects(rc, rects);
    for (int i = 0; i < BTN_N; i++)
        if (mx >= rects[i].left && mx < rects[i].right) return i;
    return -1;
}

static BOOL BtnEnabled(int i) {
    HWND hed = Editor_ActiveEdit();
    if (!hed) return FALSE;
    switch (kBtns[i].cmd) {
    case IDM_UNDO:  return SendMessage(hed, SCI_CANUNDO, 0, 0) != 0;
    case IDM_REDO:  return SendMessage(hed, SCI_CANREDO, 0, 0) != 0;
    case IDM_CUT:
    case IDM_COPY:  return SendMessage(hed, SCI_GETSELECTIONEMPTY, 0, 0) == 0;
    case IDM_PASTE: return SendMessage(hed, SCI_CANPASTE, 0, 0) != 0;
    }
    return TRUE;
}

static BOOL BtnChecked(int i) {
    switch (kBtns[i].cmd) {
    case IDM_WRAP:    return g_wordWrap;
    case IDM_THEME:   return g_dark;
    case IDM_VIEW_MD: return MdView_IsVisible();
    }
    return FALSE;
}

void Toolbar_UpdateStates(void) {
    unsigned h = 7;
    for (int i = 0; i < BTN_N; i++) {
        if (kBtns[i].icon < 0) continue;
        h = h * 31u + (unsigned)(BtnEnabled(i)) * 3u + (unsigned)(BtnChecked(i)) * 5u;
    }
    if (h != s_stateHash) {
        s_stateHash = h;
        if (g_hwndTool) InvalidateRect(g_hwndTool, NULL, FALSE);
    }
}

#define CLR_GRAY ((COLORREF)-1)

static void TbDrawIcon(HDC hdc, const RECT* r, int icon, BOOL en) {
    static const struct { COLORREF m, a; } kClr[14] = {
        { RGB(96,164,222),  CLR_GRAY       },
        { RGB(230,178,72),  RGB(230,178,72)},
        { RGB(96,164,222),  CLR_GRAY       },
        { RGB(96,164,222),  RGB(96,164,222)},
        { RGB(75,200,170),  RGB(75,200,170)},
        { CLR_GRAY,         RGB(228,110,118)},
        { CLR_GRAY,         RGB(96,164,222) },
        { CLR_GRAY,         RGB(96,164,222) },
        { RGB(96,164,222),  CLR_GRAY       },
        { CLR_GRAY,         RGB(112,185,105)},
        { CLR_GRAY,         RGB(228,110,118)},
        { RGB(177,144,250), RGB(177,144,250)},
        { RGB(230,178,72),  RGB(96,164,222) },
        { RGB(75,200,170),  CLR_GRAY       },
    };
    double sz = UI_Scale(16);
    double ox = r->left + (r->right - r->left - sz) / 2.0;
    double oy = r->top + (r->bottom - r->top - sz) / 2.0;
    double u = sz / 16.0;
    int pw = UI_Scale(1) < 1 ? 1 : UI_Scale(1);
    COLORREF gray = g_dark ? RGB(165,170,175) : RGB(95,99,104);
    if (!en) gray = g_dark ? RGB(100,100,100) : RGB(172,172,176);
    COLORREF mc = (kClr[icon].m == CLR_GRAY || !en) ? gray : kClr[icon].m;
    COLORREF ac = (kClr[icon].a == CLR_GRAY || !en) ? gray : kClr[icon].a;
#define X(v) ((int)(ox + (v) * u + 0.5))
#define Y(v) ((int)(oy + (v) * u + 0.5))
    HPEN pm = CreatePen(PS_SOLID, pw, mc);
    HPEN pa = CreatePen(PS_SOLID, pw, ac);
    HBRUSH bm = CreateSolidBrush(mc);
    HBRUSH ba = CreateSolidBrush(ac);
    HGDIOBJ op = SelectObject(hdc, pm);
    HGDIOBJ ob = SelectObject(hdc, bm);
    switch (icon) {
    case 0:
        Rectangle(hdc, X(3.5), Y(2), X(12.5), Y(14));
        MoveToEx(hdc, X(9), Y(2), NULL); LineTo(hdc, X(12.5), Y(5.5));
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(5.5), Y(8.5), NULL); LineTo(hdc, X(10.5), Y(8.5));
        MoveToEx(hdc, X(5.5), Y(11), NULL); LineTo(hdc, X(10.5), Y(11));
        SelectObject(hdc, pm);
        break;
    case 1:
        MoveToEx(hdc, X(1.5), Y(13), NULL); LineTo(hdc, X(1.5), Y(5));
        LineTo(hdc, X(6), Y(5)); LineTo(hdc, X(7.5), Y(3.5)); LineTo(hdc, X(11), Y(3.5));
        LineTo(hdc, X(11), Y(5)); LineTo(hdc, X(14.5), Y(5));
        LineTo(hdc, X(14.5), Y(13)); LineTo(hdc, X(1.5), Y(13));
        break;
    case 2:
        Rectangle(hdc, X(2.5), Y(2), X(13.5), Y(14));
        SelectObject(hdc, pa);
        Rectangle(hdc, X(5), Y(2.5), X(11), Y(6));
        Rectangle(hdc, X(5.5), Y(9), X(10.5), Y(13.5));
        SelectObject(hdc, pm);
        break;
    case 3: {
        MoveToEx(hdc, X(12), Y(4), NULL); LineTo(hdc, X(12), Y(8.5)); LineTo(hdc, X(8.5), Y(8.5));
        POINT tri[3] = { {X(5), Y(8.5)}, {X(8.5), Y(6.5)}, {X(8.5), Y(10.5)} };
        Polygon(hdc, tri, 3);
        break;
    }
    case 4: {
        MoveToEx(hdc, X(4), Y(4), NULL); LineTo(hdc, X(4), Y(8.5)); LineTo(hdc, X(7.5), Y(8.5));
        POINT tri[3] = { {X(11), Y(8.5)}, {X(7.5), Y(6.5)}, {X(7.5), Y(10.5)} };
        Polygon(hdc, tri, 3);
        break;
    }
    case 5:
        MoveToEx(hdc, X(11.5), Y(2.5), NULL); LineTo(hdc, X(4.5), Y(10.5));
        MoveToEx(hdc, X(4.5), Y(2.5), NULL); LineTo(hdc, X(11.5), Y(10.5));
        SelectObject(hdc, pa);
        Ellipse(hdc, X(2.7), Y(10.7), X(6.3), Y(14.3));
        Ellipse(hdc, X(9.7), Y(10.7), X(13.3), Y(14.3));
        SelectObject(hdc, pm);
        break;
    case 6:
        SelectObject(hdc, pa);
        Rectangle(hdc, X(5.5), Y(2), X(14), Y(10));
        SelectObject(hdc, pm);
        Rectangle(hdc, X(2), Y(6), X(10.5), Y(14));
        break;
    case 7:
        Rectangle(hdc, X(4), Y(3.5), X(12), Y(14));
        SelectObject(hdc, pa);
        Rectangle(hdc, X(6.5), Y(2), X(9.5), Y(5));
        SelectObject(hdc, pm);
        MoveToEx(hdc, X(6), Y(8), NULL); LineTo(hdc, X(10), Y(8));
        MoveToEx(hdc, X(6), Y(10.5), NULL); LineTo(hdc, X(10), Y(10.5));
        break;
    case 8:
        RoundRect(hdc, X(1.8), Y(5.0), X(6.8), Y(14.2), X(2.2), Y(2.2));
        RoundRect(hdc, X(9.2), Y(5.0), X(14.2), Y(14.2), X(2.2), Y(2.2));
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(6.8), Y(6.2), NULL); LineTo(hdc, X(8.0), Y(3.8)); LineTo(hdc, X(9.2), Y(6.2));
        SelectObject(hdc, ba);
        Rectangle(hdc, X(7.3), Y(7.5), X(8.7), Y(10.8));
        SelectObject(hdc, pm);
        SelectObject(hdc, bm);
        break;
    case 9:
    case 10:
        Ellipse(hdc, X(2.5), Y(2.5), X(11.5), Y(11.5));
        {
            HPEN hp = CreatePen(PS_SOLID, pw * 2, mc);
            SelectObject(hdc, hp);
            MoveToEx(hdc, X(10.6), Y(10.6), NULL); LineTo(hdc, X(13.8), Y(13.8));
            SelectObject(hdc, pm);
            DeleteObject(hp);
        }
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(4.8), Y(7), NULL); LineTo(hdc, X(9.2), Y(7));
        if (icon == 9) { MoveToEx(hdc, X(7), Y(4.8), NULL); LineTo(hdc, X(7), Y(9.2)); }
        SelectObject(hdc, pm);
        break;
    case 11:
        MoveToEx(hdc, X(2.5), Y(4.5), NULL); LineTo(hdc, X(13.5), Y(4.5));
        MoveToEx(hdc, X(13.5), Y(4.5), NULL); LineTo(hdc, X(13.5), Y(10));
        MoveToEx(hdc, X(13.5), Y(10), NULL); LineTo(hdc, X(9), Y(10));
        {
            POINT tri[3] = { {X(5.5), Y(10)}, {X(9), Y(8)}, {X(9), Y(12)} };
            Polygon(hdc, tri, 3);
        }
        break;
    case 12:
        SelectObject(hdc, ba);
        Pie(hdc, X(3), Y(3), X(13), Y(13), X(8), Y(3), X(8), Y(13));
        Ellipse(hdc, X(3), Y(3), X(13), Y(13));
        break;
    case 13:
        Rectangle(hdc, X(1.5), Y(2.5), X(14.5), Y(11));
        MoveToEx(hdc, X(8), Y(11), NULL); LineTo(hdc, X(8), Y(13.5));
        MoveToEx(hdc, X(5), Y(13.5), NULL); LineTo(hdc, X(11), Y(13.5));
        break;
    }
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pm);
    DeleteObject(pa);
    DeleteObject(bm);
    DeleteObject(ba);
#undef X
#undef Y
}

static void SetTip(int i) {
    if (!s_tip) return;
    if (i == s_tipBtn) return;
    s_tipBtn = i;
    if (i >= 0 && kBtns[i].icon >= 0 && kBtns[i].strId)
        _snwprintf(s_tipText, 128, L"%s", T((StrId)kBtns[i].strId));
    else
        s_tipText[0] = L'\0';
    TOOLINFO ti; memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = s_tipOwner;
    ti.uId = 0;
    ti.lpszText = s_tipText;
    SendMessage(s_tip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

static LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        s_tipOwner = hwnd;
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
        }
        return 0;
    }
    if (msg == WM_SIZE) {
        if (s_tip) {
            TOOLINFO ti; memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.hwnd = s_tipOwner;
            ti.uId = 0;
            GetClientRect(hwnd, &ti.rect);
            SendMessage(s_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_dark ? RGB(37,37,38) : RGB(225,225,228));
        FillRect(hdc, &rc, bg); DeleteObject(bg);
        RECT rects[BTN_N]; TbRects(rc, rects);
        for (int i = 0; i < BTN_N; i++) {
            if (kBtns[i].icon < 0) {
                int mx = (rects[i].left + rects[i].right) / 2;
                HPEN sp = CreatePen(PS_SOLID, 1, g_dark ? RGB(55,55,57) : RGB(206,206,210));
                HGDIOBJ old = SelectObject(hdc, sp);
                MoveToEx(hdc, mx, UI_Scale(7), NULL);
                LineTo(hdc, mx, rc.bottom - UI_Scale(7));
                SelectObject(hdc, old);
                DeleteObject(sp);
                continue;
            }
            BOOL en = BtnEnabled(i);
            BOOL chk = BtnChecked(i);
            BOOL hov = (i == s_hover);
            BOOL prs = (i == s_hover && i == s_pressBtn);
            RECT r = rects[i];
            if (prs) {
                HBRUSH hb = CreateSolidBrush(g_dark ? RGB(70,70,74) : RGB(214,214,219));
                FillRect(hdc, &r, hb); DeleteObject(hb);
            } else if (chk) {
                HBRUSH hb = CreateSolidBrush(g_dark ? RGB(60,60,62) : RGB(243,243,246));
                FillRect(hdc, &r, hb); DeleteObject(hb);
            } else if (hov && en) {
                HBRUSH hb = CreateSolidBrush(g_dark ? RGB(52,52,55) : RGB(238,238,241));
                FillRect(hdc, &r, hb); DeleteObject(hb);
            }
            TbDrawIcon(hdc, &r, kBtns[i].icon, en);
        }
        HPEN bp = CreatePen(PS_SOLID, 1, g_dark ? RGB(28,28,29) : RGB(201,201,205));
        HGDIOBJ old = SelectObject(hdc, bp);
        MoveToEx(hdc, 0, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, old);
        DeleteObject(bp);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_LBUTTONDOWN) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        int i = HitBtn(hwnd, mx, my);
        if (i >= 0 && kBtns[i].icon >= 0 && BtnEnabled(i)) {
            s_pressBtn = i;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        if (s_pressBtn >= 0) {
            ReleaseCapture();
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            int i = HitBtn(hwnd, mx, my);
            int clicked = (i == s_pressBtn && i >= 0) ? i : -1;
            s_pressBtn = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            if (clicked >= 0 && BtnEnabled(clicked))
                PostMessage(g_hwndMain, WM_COMMAND, MAKEWPARAM(kBtns[clicked].cmd, 0), 0);
        }
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (s_tip) {
            MSG m; m.hwnd = hwnd; m.message = WM_MOUSEMOVE; m.wParam = 0; m.lParam = lp;
            m.pt.x = mx; m.pt.y = my;
            SendMessage(s_tip, TTM_RELAYEVENT, 0, (LPARAM)&m);
        }
        int i = HitBtn(hwnd, mx, my);
        if (i >= 0 && kBtns[i].icon < 0) i = -1;
        if (i != s_hover) {
            s_hover = i;
            InvalidateRect(hwnd, NULL, FALSE);
            SetTip(i);
        }
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
        if (s_hover != -1) {
            s_hover = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Toolbar_Register(void) {
    WNDCLASSEXW tc; memset(&tc, 0, sizeof(tc));
    tc.cbSize = sizeof(tc);
    tc.style = CS_HREDRAW | CS_VREDRAW;
    tc.lpfnWndProc = ToolbarProc;
    tc.hInstance = g_hInst;
    tc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    tc.lpszClassName = L"ETONToolbar";
    tc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&tc);
}
