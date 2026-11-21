#include "common.h"
typedef struct { int cmd; int strId; int icon; } TbBtn;
static const TbBtn kBtns[] = {
    { 0,           STR_MENU_FILE,      -3 },
    { 2,           STR_MENU_TOOLS,     -3 },
    { 3,           STR_MENU_VIEW,      -3 },
    { 0,           0,                  -1 },
    { IDM_NEW,     STR_ITEM_NEW,        0 },
    { IDM_OPEN,    STR_ITEM_OPEN,       1 },
    { IDM_SAVE,    STR_ITEM_SAVE,       2 },
    { 0,           0,                  -1 },
    { IDM_FIND,    STR_ITEM_FIND,       8 },
    { 0,           0,                  -1 },
    { IDM_ZOOMIN,  STR_ITEM_ZOOMIN,     9 },
    { IDM_ZOOMOUT, STR_ITEM_ZOOMOUT,   10 },
    { 0,           0,                  -1 },
    { IDM_WRAP,    STR_ITEM_WRAP,      11 },
    { IDM_VIEW_MD, STR_ITEM_MDPREVIEW, 12 },
    { IDM_VIEW_MDSPLT, STR_ITEM_MDSPLIT, 13 },
    { 0,           0,                  -1 },
    { IDM_EXPORT_DOCX, STR_ITEM_EXPDOCX, 14 },
};
#define KBTN_N ((int)(sizeof(kBtns) / sizeof(kBtns[0])))
#define BTN_MAX 40
static TbBtn s_btns[BTN_MAX];
static int   s_btnN = 0;
static LangID s_curLang = (LangID)-1;
static int   s_curHasDoc = -1;
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
static BOOL RebuildButtons(void) {
    BOOL hasDoc = (g_curDoc >= 0 && g_curDoc < g_docCount);
    LangID lang = hasDoc ? g_docs[g_curDoc].lang : LANG_NONE;
    if (s_btnN && lang == s_curLang && hasDoc == s_curHasDoc) return FALSE;
    s_curLang = lang;
    s_curHasDoc = hasDoc;
    int n = 0;
    for (int k = 0; k < KBTN_N && n < BTN_MAX; k++) {
        s_btns[n++] = kBtns[k];
        if (kBtns[k].icon == -1 && k > 0 && kBtns[k - 1].cmd == IDM_SAVE && n < BTN_MAX - 3) {
            if (lang == LANG_JSON) {
                s_btns[n].cmd = IDM_JSON_FMT; s_btns[n].strId = STR_ITEM_JSONFMT; s_btns[n].icon = 15; n++;
                s_btns[n].cmd = IDM_JSON_MIN; s_btns[n].strId = STR_ITEM_JSONMIN; s_btns[n].icon = 16; n++;
                s_btns[n].cmd = 0; s_btns[n].strId = 0; s_btns[n].icon = -1; n++;
            } else if (lang == LANG_YAML) {
                s_btns[n].cmd = IDM_YAML_FMT; s_btns[n].strId = STR_ITEM_YAMLFMT; s_btns[n].icon = 17; n++;
                s_btns[n].cmd = 0; s_btns[n].strId = 0; s_btns[n].icon = -1; n++;
            }
        }
    }
    s_btnN = n;
    s_hover = -1;
    s_pressBtn = -1;
    s_tipBtn = -1;
    return TRUE;
}
static int  s_menuTxtW[7];
static int  s_menuTxtKey = -1;
static void MenuTxtWidths(HWND hwnd) {
    int key = g_fontGen * 100003 + (int)g_dpi * 31 + I18n_Lang();
    if (s_menuTxtKey == key) return;
    HDC hdc = GetDC(hwnd);
    HFONT of = (HFONT)SelectObject(hdc, g_hFont);
    for (int i = 0; i < 7; i++) {
        int k = 0;
        while (k < s_btnN && !(s_btns[k].icon == -3 && s_btns[k].cmd == i)) k++;
        if (k < s_btnN) {
            const wchar_t* t = T((StrId)s_btns[k].strId);
            SIZE sz; GetTextExtentPoint32W(hdc, t, (int)wcslen(t), &sz);
            s_menuTxtW[i] = sz.cx + UI_Scale(18);
        } else {
            s_menuTxtW[i] = UI_Scale(48);
        }
    }
    SelectObject(hdc, of); ReleaseDC(hwnd, hdc);
    s_menuTxtKey = key;
}
static int ItemW(HWND hwnd, int i) {
    if (s_btns[i].icon == -3) {
        MenuTxtWidths(hwnd);
        return s_menuTxtW[s_btns[i].cmd];
    }
    return (s_btns[i].icon < 0) ? SepW() : BtnW();
}
static void TbRects(HWND hwnd, RECT rc, RECT* out) {
    int sepIdx = 0;
    while (sepIdx < s_btnN && s_btns[sepIdx].icon != -1) sepIdx++;
    int x = PadL();
    for (int i = 0; i <= sepIdx && i < s_btnN; i++) {
        int w = ItemW(hwnd, i);
        out[i].left = x; out[i].right = x + w;
        out[i].top = 0; out[i].bottom = rc.bottom;
        x += w;
    }
    if (sepIdx >= s_btnN) return;
    int clusterW = 0;
    for (int i = sepIdx + 1; i < s_btnN; i++) clusterW += ItemW(hwnd, i);
    int cx = (rc.right - clusterW) / 2;
    if (cx < x) cx = x;
    for (int i = sepIdx + 1; i < s_btnN; i++) {
        int w = ItemW(hwnd, i);
        out[i].left = cx; out[i].right = cx + w;
        out[i].top = 0; out[i].bottom = rc.bottom;
        cx += w;
    }
}
static int HitBtn(HWND hwnd, int mx, int my) {
    RECT rc; GetClientRect(hwnd, &rc);
    if (mx < 0 || mx >= rc.right || my < 0 || my >= rc.bottom) return -1;
    RECT rects[BTN_MAX]; TbRects(hwnd, rc, rects);
    for (int i = 0; i < s_btnN; i++)
        if (mx >= rects[i].left && mx < rects[i].right) return i;
    return -1;
}
int Toolbar_MenuHit(POINT ptScreen) {
    if (!g_hwndTool) return -1;
    POINT pt = ptScreen;
    if (!ScreenToClient(g_hwndTool, &pt)) return -1;
    RECT rc; GetClientRect(g_hwndTool, &rc);
    if (pt.x < 0 || pt.x >= rc.right || pt.y < 0 || pt.y >= rc.bottom) return -1;
    RECT rects[BTN_MAX]; TbRects(g_hwndTool, rc, rects);
    for (int i = 0; i < s_btnN; i++) {
        if (s_btns[i].icon != -3) continue;
        if (pt.x >= rects[i].left && pt.x < rects[i].right)
            return s_btns[i].cmd;
    }
    return -1;
}
BOOL Toolbar_MenuItemRect(int groupIdx, RECT* rcScreen) {
    if (!g_hwndTool || !rcScreen) return FALSE;
    RECT rc; GetClientRect(g_hwndTool, &rc);
    RECT rects[BTN_MAX]; TbRects(g_hwndTool, rc, rects);
    for (int i = 0; i < s_btnN; i++) {
        if (s_btns[i].icon == -3 && s_btns[i].cmd == groupIdx) {
            *rcScreen = rects[i];
            MapWindowPoints(g_hwndTool, NULL, (POINT*)rcScreen, 2);
            return TRUE;
        }
    }
    return FALSE;
}
static BOOL BtnEnabled(int i) {
    HWND hed = Editor_ActiveEdit();
    if (!hed) return FALSE;
    switch (s_btns[i].cmd) {
    case IDM_EXPORT_DOCX:
        return g_curDoc >= 0 && g_curDoc < g_docCount &&
               g_docs[g_curDoc].lang == LANG_MD;
    }
    return TRUE;
}
static BOOL BtnChecked(int i) {
    switch (s_btns[i].cmd) {
    case IDM_WRAP:    return g_wordWrap;
    case IDM_VIEW_MD: return MdView_IsVisible();
    case IDM_VIEW_MDSPLT: return g_mdSplit;
    }
    return FALSE;
}
void Toolbar_UpdateStates(void) {
    BOOL changed = RebuildButtons();
    unsigned h = (unsigned)s_btnN * 7u + (unsigned)I18n_Lang() * 13u + 3u;
    for (int i = 0; i < s_btnN; i++) {
        if (s_btns[i].icon < 0) continue;
        h = h * 31u + (unsigned)(BtnEnabled(i)) * 3u + (unsigned)(BtnChecked(i)) * 5u;
    }
    if (changed) h = ~h;
    if (h != s_stateHash) {
        s_stateHash = h;
        if (g_hwndTool) InvalidateRect(g_hwndTool, NULL, FALSE);
    }
}
#define CLR_GRAY ((COLORREF)-1)
static void TbDrawIcon(HDC hdc, const RECT* r, int icon, BOOL en) {
    static const struct { COLORREF m, a; } kClr[18] = {
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
        { RGB(75,200,170),  RGB(75,200,170)},
        { RGB(75,200,170),  CLR_GRAY       },
        { RGB(96,164,222),  RGB(230,178,72)},
        { RGB(96,164,222),  RGB(230,178,72)},
        { RGB(230,178,72),  RGB(96,164,222)},
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
        Arc(hdc, X(1.8), Y(3.2), X(14.2), Y(12.8), X(1.8), Y(8), X(14.2), Y(8));
        Arc(hdc, X(1.8), Y(3.2), X(14.2), Y(12.8), X(14.2), Y(8), X(1.8), Y(8));
        Ellipse(hdc, X(5.7), Y(5.7), X(10.3), Y(10.3));
        Ellipse(hdc, X(6.9), Y(6.9), X(9.1), Y(9.1));
        break;
    case 13: {
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, X(1.5), Y(2.5), X(14.5), Y(13.5));
        MoveToEx(hdc, X(8), Y(2.5), NULL); LineTo(hdc, X(8), Y(13.5));
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(3.2), Y(5.6), NULL); LineTo(hdc, X(6.4), Y(5.6));
        MoveToEx(hdc, X(3.2), Y(8.0), NULL); LineTo(hdc, X(6.4), Y(8.0));
        MoveToEx(hdc, X(3.2), Y(10.4), NULL); LineTo(hdc, X(6.4), Y(10.4));
        SelectObject(hdc, pm);
        Ellipse(hdc, X(9.3), Y(6.5), X(12.7), Y(9.9));
        SelectObject(hdc, bm);
        Ellipse(hdc, X(10.3), Y(7.5), X(11.7), Y(8.9));
        break;
    }
    case 14:
        Rectangle(hdc, X(2.5), Y(2), X(9.5), Y(10));
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(4.5), Y(4.8), NULL); LineTo(hdc, X(7.5), Y(4.8));
        MoveToEx(hdc, X(4.5), Y(7.0), NULL); LineTo(hdc, X(7.5), Y(7.0));
        MoveToEx(hdc, X(5.5), Y(12.5), NULL); LineTo(hdc, X(11.5), Y(12.5));
        {
            POINT tri[3] = { {X(11.2), Y(10.1)}, {X(14.0), Y(12.5)}, {X(11.2), Y(14.9)} };
            Polygon(hdc, tri, 3);
        }
        SelectObject(hdc, pm);
        break;
    case 15:
        MoveToEx(hdc, X(9.0), Y(2.5), NULL); LineTo(hdc, X(6.0), Y(2.5));
        LineTo(hdc, X(6.0), Y(8.0)); LineTo(hdc, X(4.0), Y(8.0));
        MoveToEx(hdc, X(6.0), Y(8.0), NULL); LineTo(hdc, X(6.0), Y(13.5)); LineTo(hdc, X(9.0), Y(13.5));
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(7.0), Y(2.5), NULL); LineTo(hdc, X(10.0), Y(2.5));
        LineTo(hdc, X(10.0), Y(8.0)); LineTo(hdc, X(12.0), Y(8.0));
        MoveToEx(hdc, X(10.0), Y(8.0), NULL); LineTo(hdc, X(10.0), Y(13.5)); LineTo(hdc, X(7.0), Y(13.5));
        SelectObject(hdc, pm);
        break;
    case 16:
        MoveToEx(hdc, X(4.5), Y(2.5), NULL); LineTo(hdc, X(2.0), Y(2.5));
        LineTo(hdc, X(2.0), Y(13.5)); LineTo(hdc, X(4.5), Y(13.5));
        MoveToEx(hdc, X(11.5), Y(2.5), NULL); LineTo(hdc, X(14.0), Y(2.5));
        LineTo(hdc, X(14.0), Y(13.5)); LineTo(hdc, X(11.5), Y(13.5));
        SelectObject(hdc, pa);
        MoveToEx(hdc, X(10.5), Y(8.0), NULL); LineTo(hdc, X(5.5), Y(8.0));
        MoveToEx(hdc, X(7.5), Y(6.0), NULL); LineTo(hdc, X(5.5), Y(8.0)); LineTo(hdc, X(7.5), Y(10.0));
        MoveToEx(hdc, X(8.5), Y(6.0), NULL); LineTo(hdc, X(10.5), Y(8.0)); LineTo(hdc, X(8.5), Y(10.0));
        SelectObject(hdc, pm);
        break;
    case 17:
        MoveToEx(hdc, X(3), Y(3.5), NULL); LineTo(hdc, X(13), Y(3.5));
        MoveToEx(hdc, X(5), Y(8), NULL); LineTo(hdc, X(13), Y(8));
        MoveToEx(hdc, X(3), Y(12.5), NULL); LineTo(hdc, X(13), Y(12.5));
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
static void StripAmp(const wchar_t* src, wchar_t* dst, int cch) {
    int o = 0;
    for (int i = 0; src[i] && o < cch - 1; i++) {
        if (src[i] == L'&') {
            if (src[i + 1] == L'&') { dst[o++] = L'&'; i++; }
            continue;
        }
        dst[o++] = src[i];
    }
    dst[o] = L'\0';
}
static void SetTip(int i) {
    if (!s_tip) return;
    if (i == s_tipBtn) return;
    s_tipBtn = i;
    if (i >= 0 && s_btns[i].icon >= 0 && s_btns[i].strId)
        StripAmp(T((StrId)s_btns[i].strId), s_tipText, 128);
    else
        s_tipText[0] = L'\0';
    TOOLINFO ti; memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = s_tipOwner;
    ti.uId = 0;
    ti.lpszText = s_tipText;
    SendMessage(s_tip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}
static void ShowExportMenu(HWND hwnd, int btnIndex) {
    RECT rc; GetClientRect(hwnd, &rc);
    RECT rects[BTN_MAX]; TbRects(hwnd, rc, rects);
    POINT pt = { rects[btnIndex].left, rects[btnIndex].bottom + 1 };
    ClientToScreen(hwnd, &pt);
    HMENU m = CreatePopupMenu();
    UINT on = MF_STRING;
    I18n_OwnerAppend(m, on, IDM_EXPORT_HTML, T(STR_ITEM_EXPHTML));
    I18n_OwnerAppend(m, on, IDM_EXPORT_PDF,  T(STR_ITEM_EXPPDF));
    I18n_OwnerAppend(m, on, IDM_EXPORT_DOCX, T(STR_ITEM_EXPDOCX));
    I18n_ApplyMenuTheme(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON,
                             pt.x, pt.y, 0, g_hwndMain, NULL);
    DestroyMenu(m);
    if (cmd) PostMessage(g_hwndMain, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
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
        RECT rects[BTN_MAX]; TbRects(hwnd, rc, rects);
        for (int i = 0; i < s_btnN; i++) {
            if (s_btns[i].icon == -1) {
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
            if (s_btns[i].icon == -3) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, en ? (g_dark ? RGB(220,220,220) : RGB(30,30,30))
                                     : (g_dark ? RGB(120,120,120) : RGB(160,160,160)));
                HFONT of = (HFONT)SelectObject(hdc, g_hFont);
                wchar_t lbl[128];
                StripAmp(T((StrId)s_btns[i].strId), lbl, 128);
                DrawTextW(hdc, lbl, -1, &r,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hdc, of);
            } else {
                TbDrawIcon(hdc, &r, s_btns[i].icon, en);
            }
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
        if (i >= 0 && s_btns[i].icon != -1 && BtnEnabled(i)) {
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
            if (clicked >= 0 && BtnEnabled(clicked)) {
                if (s_btns[clicked].icon == -3) {
                    RECT wr; GetWindowRect(hwnd, &wr);
                    RECT rects[BTN_MAX]; RECT crc; GetClientRect(hwnd, &crc);
                    TbRects(hwnd, crc, rects);
                    PostMessage(g_hwndMain, WM_APP_STRIP_GROUP,
                                (WPARAM)s_btns[clicked].cmd,
                                MAKELPARAM(wr.left + (rects[clicked].left + rects[clicked].right) / 2,
                                           wr.bottom + 1));
                } else if (s_btns[clicked].cmd == IDM_EXPORT_DOCX)
                    ShowExportMenu(hwnd, clicked);
                else
                    PostMessage(g_hwndMain, WM_COMMAND, MAKEWPARAM(s_btns[clicked].cmd, 0), 0);
            }
        }
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        CaptionButton_ClearHover();
        if (s_tip) {
            MSG m; m.hwnd = hwnd; m.message = WM_MOUSEMOVE; m.wParam = 0; m.lParam = lp;
            m.pt.x = mx; m.pt.y = my;
            SendMessage(s_tip, TTM_RELAYEVENT, 0, (LPARAM)&m);
        }
        int i = HitBtn(hwnd, mx, my);
        if (i >= 0 && s_btns[i].icon == -1) i = -1;
        if (i != s_hover) {
            RECT rc; GetClientRect(hwnd, &rc);
            RECT rects[BTN_MAX]; TbRects(hwnd, rc, rects);
            if (s_hover >= 0 && s_hover < s_btnN)
                InvalidateRect(hwnd, &rects[s_hover], FALSE);
            if (i >= 0 && i < s_btnN)
                InvalidateRect(hwnd, &rects[i], FALSE);
            s_hover = i;
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
            RECT rc; GetClientRect(hwnd, &rc);
            RECT rects[BTN_MAX]; TbRects(hwnd, rc, rects);
            if (s_hover >= 0 && s_hover < s_btnN)
                InvalidateRect(hwnd, &rects[s_hover], FALSE);
            s_hover = -1;
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
    tc.hbrBackground = NULL;
    RegisterClassExW(&tc);
}
