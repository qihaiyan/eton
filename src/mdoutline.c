#include "common.h"

/* ===================== Markdown 大纲（左侧停靠面板） =====================
   逐行扫描编辑器文本提取标题（ATX # 与 Setext =/- 下划线，跳过围栏代码
   块），点击条目跳到编辑器对应行；预览可见时按行比例同步滚动预览。
   光标移动时高亮当前所在标题；输入经 400ms 去抖后重扫。 */

#define TID_RESCAN 1

typedef struct {
    int level;              /* 1..6 */
    int line;               /* 0 基源码行号 */
    wchar_t* title;
} OlEnt;

static struct {
    HWND hwnd;
    OlEnt* ents; int n, cap;
    int docIdx;
    unsigned scanGen;       /* 扫描时文档的 modGen */
    int clientW, clientH;
    int scrollY;
    int hover, cur;         /* 悬停 / 光标所在标题，-1 = 无 */
    BOOL closeHot;          /* 标题栏关闭按钮悬停 */
    BOOL sbDrag; int sbDragOff;
    HFONT font, fontBold;
    BOOL fontsOk;
} O;

static int CaptionH(void) { return UI_Scale(26); }
static int ItemH(void)    { return UI_Scale(22); }
static int SbW(void)      { return UI_Scale(12); }

/* 标题栏右侧关闭按钮矩形 */
static RECT CloseBtnRect(void) {
    int s = UI_Scale(18);
    RECT rc = { O.clientW - s - UI_Scale(5), (CaptionH() - s) / 2,
                O.clientW - UI_Scale(5), (CaptionH() + s) / 2 };
    return rc;
}

static BOOL InCloseBtn(int px, int py) {
    RECT cb = CloseBtnRect();
    return px >= cb.left && px < cb.right && py >= cb.top && py < cb.bottom;
}

int MdOutline_Width(void) { return UI_Scale(212); }

static void FontsFree(void) {
    if (O.font) DeleteObject(O.font);
    if (O.fontBold) DeleteObject(O.fontBold);
    O.font = O.fontBold = NULL;
    O.fontsOk = FALSE;
}

static void FontsEnsure(HDC hdc) {
    if (O.fontsOk) return;
    int h = -MulDiv(9, (int)g_dpi, 72);
    O.font = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    O.fontBold = CreateFontW(h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    (void)hdc;
    O.fontsOk = TRUE;
}

static void AddEnt(int level, int line, const char* s8, int len8) {
    if (O.n >= 2048) return;
    int n = MultiByteToWideChar(CP_UTF8, 0, s8, len8, NULL, 0);
    if (n <= 0) return;
    wchar_t* w = (wchar_t*)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, s8, len8, w, n);
    w[n] = L'\0';
    if (n > 160) w[160] = L'\0';
    if (O.n >= O.cap) {
        int nc = O.cap ? O.cap * 2 : 64;
        OlEnt* ne = (OlEnt*)realloc(O.ents, (size_t)nc * sizeof(OlEnt));
        if (!ne) { free(w); return; }
        O.ents = ne; O.cap = nc;
    }
    O.ents[O.n].level = level;
    O.ents[O.n].line = line;
    O.ents[O.n].title = w;
    O.n++;
}

static void Scan(int index) {
    for (int i = 0; i < O.n; i++) free(O.ents[i].title);
    O.n = 0;
    O.docIdx = index;
    O.scanGen = g_docs[index].modGen;
    O.hover = -1;
    DWORD len = 0;
    char* txt = Editor_GetTextUtf8(index, &len);
    if (!txt) return;

    char fence = 0;                 /* 开着的围栏字符（0 = 无） */
    BOOL prevIsPara = FALSE;        /* 前一行是否普通段落文本（Setext 用） */
    const char* prev = NULL; int prevLen = 0;
    const char* end = txt + len;
    const char* p = txt;
    int lineNo = 0;
    while (p < end) {
        const char* e = p;
        while (e < end && *e != '\n') e++;
        const char* ln = p;
        int ll = (int)(e - p);
        if (ll > 0 && ln[ll - 1] == '\r') ll--;
        int i = 0;
        while (i < ll && (ln[i] == ' ' || ln[i] == '\t')) i++;
        BOOL blank = (i >= ll);
        if (fence) {                /* 围栏内：只找闭合围栏 */
            if (!blank && ln[i] == fence) {
                int c2 = i;
                while (c2 < ll && ln[c2] == fence) c2++;
                int rest = c2;
                while (rest < ll && (ln[rest] == ' ' || ln[rest] == '\t')) rest++;
                if (c2 - i >= 3 && rest >= ll) fence = 0;
            }
            prevIsPara = FALSE;
        } else if (!blank) {
            if (ln[i] == '`' || ln[i] == '~') {         /* 围栏开始 */
                int c2 = i;
                while (c2 < ll && ln[c2] == ln[i]) c2++;
                if (c2 - i >= 3) {
                    fence = ln[i];
                    prevIsPara = FALSE;
                    goto nextline;
                }
            }
            if (ln[i] == '#') {                          /* ATX */
                int h = i;
                while (h < ll && ln[h] == '#') h++;
                int hn = h - i;
                if (hn >= 1 && hn <= 6 &&
                    (h >= ll || ln[h] == ' ' || ln[h] == '\t')) {
                    int b = h;
                    while (b < ll && (ln[b] == ' ' || ln[b] == '\t')) b++;
                    int t = ll;
                    while (t > b && (ln[t-1] == ' ' || ln[t-1] == '\t')) t--;
                    while (t > b && ln[t-1] == '#') t--;  /* 闭合 # 序列 */
                    while (t > b && (ln[t-1] == ' ' || ln[t-1] == '\t')) t--;
                    if (t > b) AddEnt(hn, lineNo, ln + b, t - b);
                    prevIsPara = FALSE;
                    goto nextline;
                }
            }
            if (prevIsPara) {                             /* Setext 下划线 */
                BOOL allEq = TRUE, allDash = TRUE;
                for (int k = i; k < ll; k++) {
                    if (ln[k] != '=') allEq = FALSE;
                    if (ln[k] != '-') allDash = FALSE;
                }
                if (allEq || allDash) {
                    int pb = 0;
                    while (pb < prevLen && (prev[pb] == ' ' || prev[pb] == '\t')) pb++;
                    int pt = prevLen;
                    while (pt > pb && (prev[pt-1] == ' ' || prev[pt-1] == '\t')) pt--;
                    if (pt > pb)
                        AddEnt(allEq ? 1 : 2, lineNo - 1, prev + pb, pt - pb);
                    prevIsPara = FALSE;
                    goto nextline;
                }
            }
            prev = ln; prevLen = ll; prevIsPara = TRUE;
        } else {
            prevIsPara = FALSE;
        }
nextline:
        p = (e < end) ? e + 1 : e;
        lineNo++;
    }
    free(txt);
}

static int ListH(void) { return O.clientH - CaptionH(); }
static int MaxScroll(void) {
    int m = O.n * ItemH() - ListH();
    return m > 0 ? m : 0;
}
static void ClampScroll(void) {
    if (O.scrollY < 0) O.scrollY = 0;
    int m = MaxScroll();
    if (O.scrollY > m) O.scrollY = m;
}

static void RecalcCur(void) {
    int line = -1;
    if (g_curDoc >= 0 && g_curDoc < g_docCount) {
        HWND hed = g_docs[g_curDoc].hwndEdit;
        line = (int)SendMessage(hed, SCI_LINEFROMPOSITION,
                                (WPARAM)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0), 0);
    }
    int c = -1;
    for (int i = 0; i < O.n; i++) {
        if (O.ents[i].line <= line) c = i;
        else break;
    }
    O.cur = c;
}

static void EnsureVisible(int idx) {
    int y = idx * ItemH();
    int lh = ListH();
    if (lh <= 0) return;
    if (y < O.scrollY) O.scrollY = y;
    else if (y + ItemH() > O.scrollY + lh) O.scrollY = y + ItemH() - lh;
    ClampScroll();
}

static void JumpTo(int idx) {
    if (idx < 0 || idx >= O.n) return;
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    Doc* d = &g_docs[g_curDoc];
    SendMessage(d->hwndEdit, SCI_GOTOLINE, O.ents[idx].line, 0);
    O.cur = idx;
    EnsureVisible(idx);
    InvalidateRect(O.hwnd, NULL, FALSE);
    if (MdView_IsVisible()) {
        /* 预览按行比例滚动（与分屏同步一致）；分屏下编辑器滚动本身
           也会经 SCN_UPDATEUI 再同步一次 */
        int total = (int)SendMessage(d->hwndEdit, SCI_GETLINECOUNT, 0, 0);
        double frac = total > 1 ? (double)O.ents[idx].line / (double)(total - 1) : 0.0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        MdView_SyncScrollFromEdit(frac);
    }
    if (!MdView_IsVisible() || g_mdSplit)
        SetFocus(d->hwndEdit);
}

static void Paint(void) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(O.hwnd, &ps);
    RECT rc;
    GetClientRect(O.hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(O.hwnd, &ps); return; }
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP def = (HBITMAP)SelectObject(mem, bmp);
    FontsEnsure(mem);

    COLORREF bg      = g_dark ? RGB(37,37,40)   : RGB(243,244,246);
    COLORREF capBg   = g_dark ? RGB(45,45,48)   : RGB(234,237,240);
    COLORREF capLine = g_dark ? RGB(56,58,62)   : RGB(216,220,224);
    COLORREF fg      = g_dark ? RGB(203,208,214): RGB(31,35,40);
    COLORREF muted   = g_dark ? RGB(139,148,158): RGB(110,119,129);
    COLORREF hoverBg = g_dark ? RGB(47,51,57)   : RGB(228,232,236);
    COLORREF curBg   = g_dark ? RGB(55,61,72)   : RGB(214,221,229);

    RECT full = { 0, 0, w, h };
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(mem, &full, br);
    DeleteObject(br);

    int capH = CaptionH();
    RECT cap = { 0, 0, w, capH };
    br = CreateSolidBrush(capBg);
    FillRect(mem, &cap, br);
    DeleteObject(br);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, muted);
    RECT cb = CloseBtnRect();
    /* 标题（右侧给 F6 提示与关闭按钮留位） */
    HFONT of = (HFONT)SelectObject(mem, O.fontBold);
    RECT capText = { UI_Scale(10), 0, cb.left - UI_Scale(34), capH };
    DrawTextW(mem, T(STR_MD_OUTLINE_TITLE), -1, &capText,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(mem, of);
    /* 快捷键提示 */
    HFONT ofF = (HFONT)SelectObject(mem, O.font);
    RECT hk = { 0, 0, cb.left - UI_Scale(8), capH };
    DrawTextW(mem, L"F6", -1, &hk,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SelectObject(mem, ofF);
    /* 关闭按钮：悬停红底白叉（Windows 标题按钮风格） */
    if (O.closeHot) {
        br = CreateSolidBrush(RGB(232,17,35));
        FillRect(mem, &cb, br);
        DeleteObject(br);
    }
    int in = UI_Scale(5);
    HPEN gpen = CreatePen(PS_SOLID, UI_Scale(1) < 1 ? 1 : UI_Scale(1),
                          O.closeHot ? RGB(255,255,255) : muted);
    HPEN gOld = (HPEN)SelectObject(mem, gpen);
    MoveToEx(mem, cb.left + in, cb.top + in, NULL);
    LineTo(mem, cb.right - in - 1, cb.bottom - in - 1);
    MoveToEx(mem, cb.right - in - 1, cb.top + in, NULL);
    LineTo(mem, cb.left + in, cb.bottom - in - 1);
    SelectObject(mem, gOld);
    DeleteObject(gpen);
    HPEN pen = CreatePen(PS_SOLID, 1, capLine);
    HPEN open_ = (HPEN)SelectObject(mem, pen);
    MoveToEx(mem, 0, capH - 1, NULL);
    LineTo(mem, w, capH - 1);
    SelectObject(mem, open_);
    DeleteObject(pen);

    int itemH = ItemH();
    int lh = ListH();
    if (O.n == 0) {
        if (lh > 0) {
            SetTextColor(mem, muted);
            HFONT of2 = (HFONT)SelectObject(mem, O.font);
            RECT empty = { UI_Scale(10), capH, w - UI_Scale(10), capH + lh };
            DrawTextW(mem, T(STR_MD_OUTLINE_EMPTY), -1, &empty,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem, of2);
        }
    } else {
        int first = O.scrollY / itemH;
        int last = (O.scrollY + lh) / itemH + 1;
        if (first < 0) first = 0;
        if (last >= O.n) last = O.n - 1;
        for (int i = first; i <= last; i++) {
            int y = capH + i * itemH - O.scrollY;
            RECT row = { 0, y, w - SbW(), y + itemH };
            if (i == O.cur) {
                br = CreateSolidBrush(curBg);
                FillRect(mem, &row, br);
                DeleteObject(br);
            } else if (i == O.hover) {
                br = CreateSolidBrush(hoverBg);
                FillRect(mem, &row, br);
                DeleteObject(br);
            }
            int indent = UI_Scale(10) + (O.ents[i].level - 1) * UI_Scale(12);
            RECT tr = { indent, y, w - SbW() - UI_Scale(6), y + itemH };
            SetTextColor(mem, fg);
            HFONT f = (HFONT)SelectObject(mem, O.ents[i].level <= 2 ? O.fontBold : O.font);
            DrawTextW(mem, O.ents[i].title, -1, &tr,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem, f);
        }
        /* 细滚动条（与预览一致的风格） */
        int maxS = MaxScroll();
        if (maxS > 0 && lh > 0) {
            COLORREF trackClr = g_dark ? RGB(37,37,40) : RGB(240,241,242);
            COLORREF thumbClr = g_dark ? RGB(82,86,93)  : RGB(193,198,203);
            RECT trk = { w - SbW(), capH, w, h };
            br = CreateSolidBrush(trackClr);
            FillRect(mem, &trk, br);
            DeleteObject(br);
            int thumbH = lh * lh / (O.n * itemH);
            if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
            int thumbY = capH + (maxS ? O.scrollY * (lh - thumbH) / maxS : 0);
            RECT thr = { w - SbW() + 2, thumbY, w - 2, thumbY + thumbH };
            br = CreateSolidBrush(thumbClr);
            FillRect(mem, &thr, br);
            DeleteObject(br);
        }
    }

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, def);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(O.hwnd, &ps);
}

static LRESULT CALLBACK OutlineProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            O.clientW = LOWORD(lp);
            O.clientH = HIWORD(lp);
            ClampScroll();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
            O.scrollY -= (short)HIWORD(wp) / WHEEL_DELTA * ItemH() * 3;
            ClampScroll();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            if (InCloseBtn(px, py)) {       /* 关闭 = 同 F6 开关 */
                O.closeHot = FALSE;
                MdOutline_Toggle();
                return 0;
            }
            int lh = ListH();
            int maxS = MaxScroll();
            if (maxS > 0 && px >= O.clientW - SbW() && py >= CaptionH()) {
                int thumbH = lh * lh / (O.n * ItemH());
                if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
                int thumbY = maxS ? CaptionH() + O.scrollY * (lh - thumbH) / maxS : CaptionH();
                if (py >= thumbY && py < thumbY + thumbH) {
                    O.sbDrag = TRUE;
                    O.sbDragOff = py - thumbY;
                    SetCapture(hwnd);
                } else {
                    O.scrollY += (py < thumbY ? -1 : 1) * lh * 9 / 10;
                    ClampScroll();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
            int idx = (py - CaptionH() + O.scrollY) / ItemH();
            if (py >= CaptionH()) JumpTo(idx);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            BOOL ch = InCloseBtn(px, py);
            if (ch != O.closeHot) {
                O.closeHot = ch;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (O.sbDrag && MaxScroll() > 0) {
                int lh = ListH();
                int thumbH = lh * lh / (O.n * ItemH());
                if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
                int t = py - CaptionH() - O.sbDragOff;
                int maxS = MaxScroll();
                O.scrollY = maxS * t / (lh - thumbH);
                ClampScroll();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            int idx = (py >= CaptionH()) ? (py - CaptionH() + O.scrollY) / ItemH() : -1;
            if (idx < 0 || idx >= O.n || px >= O.clientW - SbW()) idx = -1;
            if (idx != O.hover) {
                O.hover = idx;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (O.hover != -1 || O.closeHot) {   /* 悬停期间挂 MOUSELEAVE 跟踪 */
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (O.sbDrag) { O.sbDrag = FALSE; ReleaseCapture(); }
            return 0;
        case WM_MOUSELEAVE:
            if (O.hover != -1 || O.closeHot) {
                O.hover = -1;
                O.closeHot = FALSE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_TIMER:
            if (wp == TID_RESCAN) {
                KillTimer(hwnd, TID_RESCAN);
                if (MdOutline_IsVisible() && g_curDoc >= 0 && g_curDoc < g_docCount &&
                    g_docs[g_curDoc].lang == LANG_MD &&
                    g_docs[g_curDoc].modGen != O.scanGen) {
                    Scan(g_curDoc);
                    RecalcCur();
                    ClampScroll();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MdOutline_Register(void) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OutlineProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ETONMDOutline";
    RegisterClassExW(&wc);
}

void MdOutline_Create(HWND parent) {
    O.hover = O.cur = -1;
    O.docIdx = -1;
    O.hwnd = CreateWindowExW(0, L"ETONMDOutline", NULL,
                             WS_CHILD | WS_VISIBLE,
                             0, 0, 0, 0, parent, NULL, g_hInst, NULL);
    ShowWindow(O.hwnd, SW_HIDE);
}

BOOL MdOutline_IsVisible(void) {
    return O.hwnd && IsWindowVisible(O.hwnd);
}

void MdOutline_OnLayout(int x, int y, int w, int h) {
    if (!O.hwnd) return;
    SetWindowPos(O.hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MdOutline_OnDocChange(void) {
    if (!O.hwnd) return;
    BOOL show = g_mdOutline && g_curDoc >= 0 && g_curDoc < g_docCount &&
                g_docs[g_curDoc].lang == LANG_MD;
    if (show) {
        ShowWindow(O.hwnd, SW_SHOWNA);
        if (O.docIdx != g_curDoc || O.scanGen != g_docs[g_curDoc].modGen) {
            if (O.docIdx != g_curDoc) { O.scrollY = 0; O.cur = -1; }
            Scan(g_curDoc);
            RecalcCur();
            ClampScroll();
        }
        InvalidateRect(O.hwnd, NULL, FALSE);
    } else {
        ShowWindow(O.hwnd, SW_HIDE);
    }
}

void MdOutline_Toggle(void) {
    g_mdOutline = !g_mdOutline;
    MdOutline_OnDocChange();
    Editor_Layout();
}

void MdOutline_NotifyModified(int index) {
    if (!O.hwnd || !MdOutline_IsVisible()) return;
    if (index != g_curDoc) return;
    if (g_curDoc < 0 || g_curDoc >= g_docCount ||
        g_docs[g_curDoc].lang != LANG_MD) return;
    SetTimer(O.hwnd, TID_RESCAN, 400, NULL);
}

void MdOutline_OnCaretLine(int line) {
    if (!MdOutline_IsVisible()) return;
    int c = -1;
    for (int i = 0; i < O.n; i++) {
        if (O.ents[i].line <= line) c = i;
        else break;
    }
    if (c != O.cur) {
        O.cur = c;
        InvalidateRect(O.hwnd, NULL, FALSE);
    }
}

void MdOutline_OnThemeChange(void) {
    if (O.hwnd) InvalidateRect(O.hwnd, NULL, FALSE);
}

void MdOutline_OnDpiChanged(void) {
    FontsFree();
    if (O.hwnd) InvalidateRect(O.hwnd, NULL, FALSE);
}
