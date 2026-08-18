#include "common.h"
#define FT_MAX_NODES 65536
#define FT_MAX_PER_DIR 4096
typedef struct FtNode {
    wchar_t name[MAX_PATH];
    wchar_t path[MAX_PATH];
    struct FtNode* parent;
    struct FtNode* firstChild;
    struct FtNode* next;
    int level;
    BOOL dir;
    BOOL expanded;
} FtNode;
static struct {
    HWND hwnd;
    FtNode* root;
    wchar_t rootDir[MAX_PATH];
    FtNode** rows; int nRows, capRows;
    int nodeCount;
    int cur;
    int clientW, clientH;
    int scrollY;
    int hover;
    BOOL closeHot;
    BOOL sbDrag; int sbDragOff;
    HFONT font, fontBold;
    BOOL fontsOk;
} F;
static int CaptionH(void) { return UI_Scale(26); }
static int ItemH(void)    { return UI_Scale(22); }
static int SbW(void)      { return UI_Scale(12); }
static int ArrowW(void)   { return UI_Scale(12); }
static RECT CloseBtnRect(void) {
    int s = UI_Scale(18);
    RECT rc = { F.clientW - s - UI_Scale(5), (CaptionH() - s) / 2,
                F.clientW - UI_Scale(5), (CaptionH() + s) / 2 };
    return rc;
}
static BOOL InCloseBtn(int px, int py) {
    RECT cb = CloseBtnRect();
    return px >= cb.left && px < cb.right && py >= cb.top && py < cb.bottom;
}
int FileTree_Width(void) { return UI_Scale(232); }
static void FontsFree(void) {
    if (F.font) DeleteObject(F.font);
    if (F.fontBold) DeleteObject(F.fontBold);
    F.font = F.fontBold = NULL;
    F.fontsOk = FALSE;
}
static void FontsEnsure(HDC hdc) {
    if (F.fontsOk) return;
    int h = -MulDiv(9, (int)g_dpi, 72);
    F.font = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    F.fontBold = CreateFontW(h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    (void)hdc;
    F.fontsOk = TRUE;
}
static void FreeChildren(FtNode* n) {
    FtNode* c = n->firstChild;
    while (c) {
        FtNode* nx = c->next;
        FreeChildren(c);
        free(c);
        F.nodeCount--;
        c = nx;
    }
    n->firstChild = NULL;
}
static void FreeTree(void) {
    if (F.root) {
        FreeChildren(F.root);
        free(F.root);
        F.nodeCount--;
        F.root = NULL;
    }
    F.rootDir[0] = L'\0';
    F.nRows = 0;
    F.cur = -1;
}
static int CmpNodes(const void* a, const void* b) {
    const FtNode* x = *(const FtNode* const*)a;
    const FtNode* y = *(const FtNode* const*)b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return _wcsicmp(x->name, y->name);
}
static void Enumerate(FtNode* dir) {
    FreeChildren(dir);
    if (dir->level >= 40 || F.nodeCount >= FT_MAX_NODES) return;
    wchar_t pattern[MAX_PATH];
    WFmt(pattern, L"%s\\*", dir->path);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    FtNode** arr = (FtNode**)malloc(sizeof(FtNode*) * FT_MAX_PER_DIR);
    int n = 0;
    if (arr) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            if (n >= FT_MAX_PER_DIR) break;
            if (F.nodeCount >= FT_MAX_NODES) break;
            FtNode* c = (FtNode*)calloc(1, sizeof(FtNode));
            if (!c) break;
            wcscpy_s(c->name, MAX_PATH, fd.cFileName);
            WFmt(c->path, L"%s\\%s", dir->path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                c->dir = TRUE;
                c->path[MAX_PATH - 1] = L'\0';
            }
            c->parent = dir;
            c->level = dir->level + 1;
            arr[n++] = c;
            F.nodeCount++;
        } while (FindNextFileW(h, &fd));
    }
    FindClose(h);
    if (n > 1) qsort(arr, n, sizeof(FtNode*), CmpNodes);
    FtNode** link = &dir->firstChild;
    for (int i = 0; i < n; i++) {
        *link = arr[i];
        link = &arr[i]->next;
    }
    free(arr);
}
static void RowsPush(FtNode* n) {
    if (F.nRows >= F.capRows) {
        int nc = F.capRows ? F.capRows * 2 : 256;
        FtNode** nr = (FtNode**)realloc(F.rows, (size_t)nc * sizeof(FtNode*));
        if (!nr) return;
        F.rows = nr;
        F.capRows = nc;
    }
    F.rows[F.nRows++] = n;
    if (n->dir && n->expanded)
        for (FtNode* c = n->firstChild; c; c = c->next) RowsPush(c);
}
static void BuildRows(void) {
    F.nRows = 0;
    if (F.root) RowsPush(F.root);
}
static void UpdateCur(void) {
    F.cur = -1;
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    if (!g_docs[g_curDoc].path[0]) return;
    for (int i = 0; i < F.nRows; i++) {
        if (!F.rows[i]->dir && _wcsicmp(F.rows[i]->path, g_docs[g_curDoc].path) == 0) {
            F.cur = i;
            break;
        }
    }
}
static void SetRoot(const wchar_t* dir) {
    FreeTree();
    FtNode* r = (FtNode*)calloc(1, sizeof(FtNode));
    if (!r) return;
    wcscpy_s(F.rootDir, MAX_PATH, dir);
    wcscpy_s(r->path, MAX_PATH, dir);
    const wchar_t* base = wcsrchr(dir, L'\\');
    wcscpy_s(r->name, MAX_PATH, base ? base + 1 : dir);
    r->dir = TRUE;
    r->expanded = TRUE;
    r->level = 0;
    F.root = r;
    F.nodeCount = 1;
    Enumerate(r);
    BuildRows();
    F.scrollY = 0;
}
static int ListH(void) { return F.clientH - CaptionH(); }
static int MaxScroll(void) {
    int m = F.nRows * ItemH() - ListH();
    return m > 0 ? m : 0;
}
static void ClampScroll(void) {
    if (F.scrollY < 0) F.scrollY = 0;
    int m = MaxScroll();
    if (F.scrollY > m) F.scrollY = m;
}
static void EnsureVisible(int idx) {
    int y = idx * ItemH();
    int lh = ListH();
    if (lh <= 0) return;
    if (y < F.scrollY) F.scrollY = y;
    else if (y + ItemH() > F.scrollY + lh) F.scrollY = y + ItemH() - lh;
    ClampScroll();
}
static void ToggleNode(FtNode* n) {
    if (!n || !n->dir) return;
    if (n->expanded) {
        n->expanded = FALSE;
    } else {
        Enumerate(n);
        n->expanded = TRUE;
    }
    BuildRows();
    UpdateCur();
    ClampScroll();
    InvalidateRect(F.hwnd, NULL, FALSE);
}
static void Paint(void) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(F.hwnd, &ps);
    RECT rc;
    GetClientRect(F.hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(F.hwnd, &ps); return; }
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
    HFONT of = (HFONT)SelectObject(mem, F.fontBold);
    RECT capText = { UI_Scale(10), 0, cb.left - UI_Scale(34), capH };
    DrawTextW(mem, T(STR_FTREE_TITLE), -1, &capText,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(mem, of);
    HFONT ofF = (HFONT)SelectObject(mem, F.font);
    RECT hk = { 0, 0, cb.left - UI_Scale(8), capH };
    DrawTextW(mem, L"F7", -1, &hk,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SelectObject(mem, ofF);
    if (F.closeHot) {
        br = CreateSolidBrush(RGB(232,17,35));
        FillRect(mem, &cb, br);
        DeleteObject(br);
    }
    int in = UI_Scale(5);
    HPEN gpen = CreatePen(PS_SOLID, UI_Scale(1) < 1 ? 1 : UI_Scale(1),
                          F.closeHot ? RGB(255,255,255) : muted);
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
    if (!F.root) {
        if (lh > 0) {
            SetTextColor(mem, muted);
            HFONT of2 = (HFONT)SelectObject(mem, F.font);
            RECT empty = { UI_Scale(10), capH, w - UI_Scale(10), capH + lh };
            DrawTextW(mem, T(STR_FTREE_EMPTY), -1, &empty,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem, of2);
        }
    } else {
        int first = F.scrollY / itemH;
        int last = (F.scrollY + lh) / itemH + 1;
        if (first < 0) first = 0;
        if (last >= F.nRows) last = F.nRows - 1;
        int a = UI_Scale(4);
        for (int i = first; i <= last; i++) {
            FtNode* n = F.rows[i];
            int y = capH + i * itemH - F.scrollY;
            RECT row = { 0, y, w - SbW(), y + itemH };
            if (i == F.cur) {
                br = CreateSolidBrush(curBg);
                FillRect(mem, &row, br);
                DeleteObject(br);
            } else if (i == F.hover) {
                br = CreateSolidBrush(hoverBg);
                FillRect(mem, &row, br);
                DeleteObject(br);
            }
            int indent = UI_Scale(8) + n->level * UI_Scale(14);
            if (n->dir) {
                int cx = indent + ArrowW() / 2;
                int cy = y + itemH / 2;
                POINT tri[3];
                if (n->expanded) {
                    tri[0].x = cx - a; tri[0].y = cy - a / 2;
                    tri[1].x = cx + a; tri[1].y = cy - a / 2;
                    tri[2].x = cx;     tri[2].y = cy + a;
                } else {
                    tri[0].x = cx - a / 2; tri[0].y = cy - a;
                    tri[1].x = cx + a;     tri[1].y = cy;
                    tri[2].x = cx - a / 2; tri[2].y = cy + a;
                }
                HBRUSH ab = CreateSolidBrush(n->expanded ? fg : muted);
                {
                    HPEN np = CreatePen(PS_SOLID, 1, n->expanded ? fg : muted);
                    HPEN po = (HPEN)SelectObject(mem, np);
                    HBRUSH bo = (HBRUSH)SelectObject(mem, ab);
                    Polygon(mem, tri, 3);
                    SelectObject(mem, po);
                    SelectObject(mem, bo);
                    DeleteObject(np);
                }
                DeleteObject(ab);
            }
            RECT tr = { indent + ArrowW(), y, w - SbW() - UI_Scale(6), y + itemH };
            SetTextColor(mem, fg);
            HFONT f = (HFONT)SelectObject(mem, n->dir ? F.fontBold : F.font);
            DrawTextW(mem, n->name, -1, &tr,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem, f);
        }
        int maxS = MaxScroll();
        if (maxS > 0 && lh > 0) {
            COLORREF trackClr = g_dark ? RGB(37,37,40) : RGB(240,241,242);
            COLORREF thumbClr = g_dark ? RGB(82,86,93)  : RGB(193,198,203);
            RECT trk = { w - SbW(), capH, w, h };
            br = CreateSolidBrush(trackClr);
            FillRect(mem, &trk, br);
            DeleteObject(br);
            int thumbH = lh * lh / (F.nRows * itemH);
            if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
            int thumbY = capH + (maxS ? F.scrollY * (lh - thumbH) / maxS : 0);
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
    EndPaint(F.hwnd, &ps);
}
static LRESULT CALLBACK FileTreeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            F.clientW = LOWORD(lp);
            F.clientH = HIWORD(lp);
            ClampScroll();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
            F.scrollY -= (short)HIWORD(wp) / WHEEL_DELTA * ItemH() * 3;
            ClampScroll();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            if (InCloseBtn(px, py)) {
                F.closeHot = FALSE;
                FileTree_Toggle();
                return 0;
            }
            int lh = ListH();
            int maxS = MaxScroll();
            if (maxS > 0 && px >= F.clientW - SbW() && py >= CaptionH()) {
                int thumbH = lh * lh / (F.nRows * ItemH());
                if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
                int thumbY = maxS ? CaptionH() + F.scrollY * (lh - thumbH) / maxS : CaptionH();
                if (py >= thumbY && py < thumbY + thumbH) {
                    F.sbDrag = TRUE;
                    F.sbDragOff = py - thumbY;
                    SetCapture(hwnd);
                } else {
                    F.scrollY += (py < thumbY ? -1 : 1) * lh * 9 / 10;
                    ClampScroll();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
            int idx = (py - CaptionH() + F.scrollY) / ItemH();
            if (py >= CaptionH() && idx >= 0 && idx < F.nRows) {
                FtNode* n = F.rows[idx];
                if (n->dir) ToggleNode(n);
                else OpenFileByPath(n->path);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            BOOL ch = InCloseBtn(px, py);
            if (ch != F.closeHot) {
                F.closeHot = ch;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (F.sbDrag && MaxScroll() > 0) {
                int lh = ListH();
                int thumbH = lh * lh / (F.nRows * ItemH());
                if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
                int t = py - CaptionH() - F.sbDragOff;
                int maxS = MaxScroll();
                F.scrollY = maxS * t / (lh - thumbH);
                ClampScroll();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            int idx = (py >= CaptionH()) ? (py - CaptionH() + F.scrollY) / ItemH() : -1;
            if (idx < 0 || idx >= F.nRows || px >= F.clientW - SbW()) idx = -1;
            if (idx != F.hover) {
                F.hover = idx;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (F.hover != -1 || F.closeHot) {
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
            if (F.sbDrag) { F.sbDrag = FALSE; ReleaseCapture(); }
            return 0;
        case WM_MOUSELEAVE:
            if (F.hover != -1 || F.closeHot) {
                F.hover = -1;
                F.closeHot = FALSE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_DESTROY:
            FreeTree();
            if (F.rows) {
                free(F.rows);
                F.rows = NULL;
                F.capRows = 0;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void FileTree_Register(void) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FileTreeProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ETONFileTree";
    RegisterClassExW(&wc);
}
void FileTree_Create(HWND parent) {
    F.hover = F.cur = -1;
    F.hwnd = CreateWindowExW(0, L"ETONFileTree", NULL,
                             WS_CHILD | WS_VISIBLE,
                             0, 0, 0, 0, parent, NULL, g_hInst, NULL);
    ShowWindow(F.hwnd, SW_HIDE);
}
BOOL FileTree_IsVisible(void) {
    return F.hwnd && IsWindowVisible(F.hwnd);
}
void FileTree_OnLayout(int x, int y, int w, int h) {
    if (!F.hwnd) return;
    SetWindowPos(F.hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}
void FileTree_OnDocChange(void) {
    if (!F.hwnd) return;
    if (!g_fileTree) {
        ShowWindow(F.hwnd, SW_HIDE);
        return;
    }
    ShowWindow(F.hwnd, SW_SHOWNA);
    if (g_curDoc >= 0 && g_curDoc < g_docCount && g_docs[g_curDoc].path[0]) {
        wchar_t dir[MAX_PATH];
        wcscpy_s(dir, MAX_PATH, g_docs[g_curDoc].path);
        wchar_t* slash = wcsrchr(dir, L'\\');
        if (slash && slash != dir) {
            *slash = L'\0';
            if (_wcsicmp(dir, F.rootDir) != 0) {
                SetRoot(dir);
            } else {
                UpdateCur();
                if (F.cur >= 0) EnsureVisible(F.cur);
            }
        }
    } else {
        UpdateCur();
    }
    InvalidateRect(F.hwnd, NULL, FALSE);
}
void FileTree_Toggle(void) {
    g_fileTree = !g_fileTree;
    FileTree_OnDocChange();
    Editor_Layout();
}
void FileTree_OnThemeChange(void) {
    if (F.hwnd) InvalidateRect(F.hwnd, NULL, FALSE);
}
void FileTree_OnDpiChanged(void) {
    FontsFree();
    if (F.hwnd) InvalidateRect(F.hwnd, NULL, FALSE);
}
