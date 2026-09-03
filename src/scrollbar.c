#include "common.h"

/* ---------- 自绘滚动条（窗口类 "ETONScrollBar"，覆盖编辑区边缘） ----------
   Scintilla 的滚动条是窗口非客户区滚动条（WS_HSCROLL/WS_VSCROLL），
   由系统按经典浅色样式绘制、无法随应用主题变化，故在 ApplyScintillaStyle
   里禁用原生滚动条（SCI_SETVSCROLLBAR/HSCROLLBAR = 0），改用这里的自绘条。
   垂直条常显；水平条在非换行模式下显示。 */

#define SB_INSET_96     2
#define SB_THICK_96     12
#define SB_MINTHUMB_96  24

typedef struct {
    HWND hwnd;
    BOOL horiz;      /* FALSE=垂直 TRUE=水平 */
    BOOL dragging;
    BOOL hover;
    double grab;     /* 按下点相对 thumb 起点的偏移（px） */
} SBState;

static SBState g_sb[2];                 /* [0] 垂直 [1] 水平 */
static double s_pos[2], s_frac[2];      /* thumb 起点/长度比例（0..1） */
static BOOL s_show[2] = { FALSE, FALSE };
static RECT s_rc[2];                    /* 由 Editor_Layout 写入的目标矩形 */
static BOOL s_haveLayout = FALSE;

int ScrollBars_Thickness(void) { return UI_Scale(SB_THICK_96); }

static HWND Hed(void) { return Editor_ActiveEdit(); }

/* 沿滚动方向的几何量：轨道长 / thumb 长 / thumb 起点 */
static void SBGeom(SBState* st, int* trackLen, int* thumbLen, int* thumbPos) {
    RECT rc; GetClientRect(st->hwnd, &rc);
    int len = st->horiz ? rc.right : rc.bottom;
    int inset = UI_Scale(SB_INSET_96);
    int tlen = len - 2 * inset;
    if (tlen < UI_Scale(10)) tlen = UI_Scale(10);
    int th = (int)(s_frac[st->horiz] * tlen + 0.5);
    int mint = UI_Scale(SB_MINTHUMB_96);
    if (th < mint) th = mint;
    if (th > tlen) th = tlen;
    *trackLen = tlen;
    *thumbLen = th;
    *thumbPos = inset + (int)(s_pos[st->horiz] * (tlen - th) + 0.5);
}

static double PtAlong(SBState* st, LPARAM lp) {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    return st->horiz ? x : y;
}

/* 按比例（0..1）滚动编辑区 */
static void SBScrollTo(SBState* st, double ratio) {
    HWND hed = Hed();
    if (!hed) return;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    if (!st->horiz) {
        int total = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
        int vis = (int)SendMessage(hed, SCI_LINESONSCREEN, 0, 0);
        int scrollable = total - vis;
        if (scrollable < 0) scrollable = 0;
        SendMessage(hed, SCI_SETFIRSTVISIBLELINE, (WPARAM)(int)(ratio * scrollable + 0.5), 0);
    } else {
        RECT rc; GetClientRect(hed, &rc);
        int visW = rc.right - (int)SendMessage(hed, SCI_GETMARGINWIDTHN, 0, 0)
                         - (int)SendMessage(hed, SCI_GETMARGINWIDTHN, 1, 0);
        if (visW < 1) visW = 1;
        int sw = (int)SendMessage(hed, SCI_GETSCROLLWIDTH, 0, 0);
        if (sw < visW) sw = visW;
        int maxX = sw - visW;
        SendMessage(hed, SCI_SETXOFFSET, (WPARAM)(int)(ratio * maxX + 0.5), 0);
    }
}

static void SBHover(SBState* st, BOOL on) {
    if (st->hover == on) return;
    st->hover = on;
    InvalidateRect(st->hwnd, NULL, FALSE);
    if (on) {
        TRACKMOUSEEVENT tme; memset(&tme, 0, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = st->hwnd;
        TrackMouseEvent(&tme);
    }
}

static LRESULT CALLBACK SBProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SBState* st = (SBState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH tb = CreateSolidBrush(g_dark ? RGB(37, 37, 38) : RGB(238, 238, 238));
        FillRect(hdc, &rc, tb); DeleteObject(tb);
        int trackLen, thumbLen, thumbPos;
        SBGeom(st, &trackLen, &thumbLen, &thumbPos);
        int inset = UI_Scale(SB_INSET_96);
        RECT th;
        if (st->horiz) SetRect(&th, thumbPos, inset, thumbPos + thumbLen, rc.bottom - inset);
        else           SetRect(&th, inset, thumbPos, rc.right - inset, thumbPos + thumbLen);
        COLORREF c = st->dragging ? (g_dark ? RGB(150, 150, 150) : RGB(150, 150, 150))
                   : st->hover    ? (g_dark ? RGB(120, 120, 120) : RGB(180, 180, 180))
                   :                (g_dark ? RGB(90, 90, 90)   : RGB(205, 205, 205));
        HBRUSH hb = CreateSolidBrush(c);
        FillRect(hdc, &th, hb); DeleteObject(hb);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_LBUTTONDOWN) {
        int trackLen, thumbLen, thumbPos;
        SBGeom(st, &trackLen, &thumbLen, &thumbPos);
        double pt = PtAlong(st, lp);
        if (pt >= thumbPos && pt <= thumbPos + thumbLen) {
            st->grab = pt - thumbPos;
        } else {
            st->grab = thumbLen / 2.0;   /* 点击轨道：以点击处为中心跳转 */
            SBScrollTo(st, (pt - st->grab - UI_Scale(SB_INSET_96)) / (trackLen - thumbLen));
        }
        st->dragging = TRUE;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        if (st->dragging && GetCapture() == hwnd) {
            int trackLen, thumbLen, thumbPos;
            SBGeom(st, &trackLen, &thumbLen, &thumbPos);
            double pt = PtAlong(st, lp);
            if (trackLen > thumbLen)
                SBScrollTo(st, (pt - st->grab - UI_Scale(SB_INSET_96)) / (trackLen - thumbLen));
        } else {
            SBHover(st, TRUE);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        if (st->dragging) {
            st->dragging = FALSE;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        SBHover(st, FALSE);
        return 0;
    }
    if (msg == WM_CAPTURECHANGED) {
        st->dragging = FALSE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ScrollBars_Create(HWND parent) {
    static BOOL registered = FALSE;
    if (!registered) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SBProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = L"ETONScrollBar";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = TRUE;
    }
    for (int i = 0; i < 2; i++) {
        g_sb[i].horiz = (i == 1);
        g_sb[i].hwnd = CreateWindowExW(0, L"ETONScrollBar", NULL,
                                       WS_CHILD, 0, 0, 0, 0, parent, NULL, g_hInst, NULL);
        SetWindowLongPtrW(g_sb[i].hwnd, GWLP_USERDATA, (LONG_PTR)&g_sb[i]);
    }
}

/* Editor_Layout 调用：设定滚动条区域（编辑区右缘/底缘） */
void ScrollBars_Layout(int x, int y, int w, int h, int thickness, BOOL showH) {
    if (!g_sb[0].hwnd) return;
    SetRect(&s_rc[0], x + w - thickness, y, x + w, y + h);
    SetRect(&s_rc[1], x, y + h - thickness, x + w - thickness, y + h);
    s_haveLayout = TRUE;
    (void)showH;   /* 水平条显隐由 ScrollBars_Update 依据换行状态决定 */
    SetWindowPos(g_sb[0].hwnd, NULL, s_rc[0].left, s_rc[0].top,
                 thickness, h, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_sb[1].hwnd, NULL, s_rc[1].left, s_rc[1].top,
                 w - thickness, thickness, SWP_NOZORDER | SWP_NOACTIVATE);
    ScrollBars_Update();
}

/* 与活动文档同步 thumb 位置/长度和显隐（SCN_UPDATEUI 等时机调用） */
void ScrollBars_Update(void) {
    HWND hed = Hed();
    BOOL showV = FALSE, showH = FALSE;
    if (hed && g_curDoc >= 0 && s_haveLayout) {
        /* 垂直：行 */
        int total = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
        int vis = (int)SendMessage(hed, SCI_LINESONSCREEN, 0, 0);
        if (vis < 1) vis = 1;
        showV = TRUE;
        if (total > vis) {
            s_frac[0] = vis / (double)total;
            s_pos[0] = (int)SendMessage(hed, SCI_GETFIRSTVISIBLELINE, 0, 0) / (double)(total - vis);
        } else {
            s_frac[0] = 1.0; s_pos[0] = 0;
        }
        /* 水平：像素（换行模式下 Scintilla 禁用横向滚动） */
        if (!g_wordWrap) {
            RECT rc; GetClientRect(hed, &rc);
            int visW = rc.right - (int)SendMessage(hed, SCI_GETMARGINWIDTHN, 0, 0)
                             - (int)SendMessage(hed, SCI_GETMARGINWIDTHN, 1, 0);
            if (visW < 1) visW = 1;
            int sw = (int)SendMessage(hed, SCI_GETSCROLLWIDTH, 0, 0);
            if (sw < visW) sw = visW;
            int xo = (int)SendMessage(hed, SCI_GETXOFFSET, 0, 0);
            int maxX = sw - visW;
            showH = TRUE;
            s_frac[1] = visW / (double)sw;
            s_pos[1] = maxX > 0 ? xo / (double)maxX : 0;
        }
    }
    for (int i = 0; i < 2; i++) {
        BOOL show = (i == 0) ? showV : showH;
        if (show != s_show[i]) {
            s_show[i] = show;
            if (g_sb[i].hwnd) ShowWindow(g_sb[i].hwnd, show ? SW_SHOW : SW_HIDE);
        }
        if (show && g_sb[i].hwnd) InvalidateRect(g_sb[i].hwnd, NULL, FALSE);
    }
}

void ScrollBars_Repaint(void) {
    for (int i = 0; i < 2; i++)
        if (g_sb[i].hwnd && s_show[i]) InvalidateRect(g_sb[i].hwnd, NULL, FALSE);
}
