#include <windows.h>
#include <gdiplus.h>
#include <math.h>
#include "meraa.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static ULONG_PTR g_tok = 0;
static BOOL g_gaReady = FALSE;

/* GDI+ 必须在任何 GDI+ 对象构造之前启动，而 C++ 的局部对象在函数体开始前就
   构造了（GraphicsPath path; 会先于 EnsureGA() 执行），首次调用会拿到
   InvalidParameter。所以改在静态初始化阶段启动。 */
struct GAInit {
    GAInit(void) {
        GdiplusStartupInput si;
        if (GdiplusStartup(&g_tok, &si, NULL) == Ok) g_gaReady = TRUE;
    }
};
static GAInit g_gaInit;

static BOOL EnsureGA(void) {
    return g_gaReady;
}

/* 注意：GDI+ 的 Graphics(HDC) 会自己带上 DC 的 viewport origin，所以调用方
   直接传 GDI 逻辑坐标即可，不要再手动叠加 GetViewportOrgEx 的偏移。 */

static Color ToColor(COLORREF c) {
    return Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

/* ---------------- 显式颜色的图元 ---------------- */

BOOL MerAAPolyline(HDC hdc, const float* xy, int n, COLORREF col, int width, BOOL dash) {
    if (!hdc || !xy || n < 2) return FALSE;
    if (!EnsureGA()) return FALSE;
    {
        Graphics g(hdc);
        int w = width > 0 ? width : 1;
        /* PixelOffsetModeNone 下整数坐标落在像素中心：奇数宽正好压满一列，
           偶数宽以整数为中线时只盖住左右各半列（实测 94/159/94），看着仍是
           1px 亮线带灰边。偶宽整体偏半像素，才能得到实心的两列。 */
        REAL off = (w % 2) ? 0.0f : 0.5f;
        PointF* tmp = NULL;
        const PointF* pts = reinterpret_cast<const PointF*>(xy);
        Status st;
        if (g.GetLastStatus() != Ok) return FALSE;
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeNone);
        Color c = ToColor(col);
        Pen pen(c, (REAL)w);
        pen.SetLineJoin(LineJoinRound);
        if (dash) {
            /* 系统 DashStyle 在 1px 抗锯齿下每段太短、整体发灰，这里用更长的图案 */
            REAL pat[2];
            pat[0] = 3.0f;
            pat[1] = 2.0f;
            pen.SetDashPattern(pat, 2);
        }
        if (off > 0.0f) {
            tmp = new PointF[n];
            if (!tmp) return FALSE;
            for (int i = 0; i < n; i++) {
                tmp[i].X = pts[i].X + off;
                tmp[i].Y = pts[i].Y + off;
            }
            pts = tmp;
        }
        st = g.DrawLines(&pen, pts, n);
        delete[] tmp;
        if (st != Ok) return FALSE;
    }
    return TRUE;
}

BOOL MerAAPolygon(HDC hdc, const float* xy, int n, COLORREF col, BOOL filled) {
    if (!hdc || !xy || n < 2) return FALSE;
    if (!EnsureGA()) return FALSE;
    {
        Graphics g(hdc);
        if (g.GetLastStatus() != Ok) return FALSE;
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeNone);
        Color c = ToColor(col);
        if (filled) {
            SolidBrush br(c);
            if (g.FillPolygon(&br, reinterpret_cast<const PointF*>(xy), n) != Ok) return FALSE;
        } else {
            Pen pen(c, 1.0f);
            pen.SetLineJoin(LineJoinRound);
            if (g.DrawPolygon(&pen, reinterpret_cast<const PointF*>(xy), n) != Ok) return FALSE;
        }
    }
    return TRUE;
}

/* ---------------- 按 DC 当前 pen / brush 绘制的图元 ----------------
 *
 * 调用点原本是「SelectObject 设 pen/brush → 调 GDI 函数」，这里直接读 DC 的
 * 当前状态，所以调用点只要换函数名，结构不用动。
 */

typedef struct {
    BOOL penOk;      /* 有可用的实笔（非 PS_NULL） */
    BOOL dash;
    int  width;
    COLORREF penCol;
    BOOL brushOk;    /* 有可用的实心刷（非 NULL_BRUSH / 花纹刷） */
    COLORREF brCol;
} DcStyle;

static void ReadDcStyle(HDC hdc, DcStyle* s) {
    LOGPEN lp;
    LOGBRUSH lb;
    HPEN hp;
    HBRUSH hb;
    memset(s, 0, sizeof(*s));
    s->width = 1;

    hp = (HPEN)GetCurrentObject(hdc, OBJ_PEN);
    if (hp && GetObjectW(hp, sizeof(lp), &lp) == sizeof(lp)) {
        if (lp.lopnWidth.x > 0) s->width = lp.lopnWidth.x;
        s->penCol = lp.lopnColor;
        s->penOk = (lp.lopnStyle != PS_NULL);
        if (lp.lopnStyle == PS_DASH || lp.lopnStyle == PS_DOT ||
            lp.lopnStyle == PS_DASHDOT || lp.lopnStyle == PS_DASHDOTDOT)
            s->dash = TRUE;
    }

    hb = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
    if (hb && GetObjectW(hb, sizeof(lb), &lb) == sizeof(lb)) {
        if (lb.lbStyle == BS_SOLID) {
            s->brushOk = TRUE;
            s->brCol = lb.lbColor;
        }
    }
}

/* 先填充后描边，与 GDI 的画法顺序一致 */
static void StrokeFill(HDC hdc, GraphicsPath* path) {
    DcStyle st;
    Graphics g(hdc);
    if (g.GetLastStatus() != Ok) return;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeNone);
    ReadDcStyle(hdc, &st);
    if (st.brushOk) {
        SolidBrush br(ToColor(st.brCol));
        g.FillPath(&br, path);
    }
    if (st.penOk) {
        Pen pen(ToColor(st.penCol), (REAL)st.width);
        pen.SetLineJoin(LineJoinRound);
        if (st.dash) {
            REAL pat[2];
            pat[0] = 3.0f;
            pat[1] = 2.0f;
            pen.SetDashPattern(pat, 2);
        }
        g.DrawPath(&pen, path);
    }
}

/* GDI 的矩形描边压在框内侧（1px 时占 x=l 与 x=r-1 两列像素），GDI+ 则是笔心
   压在路径上。PixelOffsetModeNone 下坐标 x.0 正对像素 x 的中心，所以笔心要放
   在「最外侧像素 + (笔宽-1)/2」处，两边各让这么多，换过去以后框不偏移不加粗。 */
static int PenWidthFor(HDC hdc) {
    LOGPEN lp;
    HPEN hp = (HPEN)GetCurrentObject(hdc, OBJ_PEN);
    if (hp && GetObjectW(hp, sizeof(lp), &lp) == sizeof(lp))
        return lp.lopnWidth.x > 0 ? lp.lopnWidth.x : 1;
    return 1;
}

static void InsetRect(HDC hdc, int l, int t, int r, int b, REAL* out) {
    int w = PenWidthFor(hdc);
    REAL h = (REAL)(w - 1) / 2.0f;
    REAL L = (REAL)l + h;
    REAL T = (REAL)t + h;
    REAL R = (REAL)(r - 1) - h;
    REAL B = (REAL)(b - 1) - h;
    if (R < L + 0.5f) R = L + 0.5f;
    if (B < T + 0.5f) B = T + 0.5f;
    out[0] = L;
    out[1] = T;
    out[2] = R - L;
    out[3] = B - T;
}

BOOL MerAARectDC(HDC hdc, int l, int t, int r, int b) {
    GraphicsPath path;
    REAL rc[4];
    if (!hdc || !EnsureGA()) return FALSE;
    InsetRect(hdc, l, t, r, b, rc);
    path.AddRectangle(RectF(rc[0], rc[1], rc[2], rc[3]));
    StrokeFill(hdc, &path);
    return TRUE;
}

BOOL MerAARoundRectDC(HDC hdc, int l, int t, int r, int b, int rx, int ry) {
    GraphicsPath path;
    REAL rc[4], L, T, R, B, dx, dy;
    if (!hdc || !EnsureGA()) return FALSE;
    InsetRect(hdc, l, t, r, b, rc);
    L = rc[0];
    T = rc[1];
    R = rc[0] + rc[2];
    B = rc[1] + rc[3];
    dx = (REAL)rx;
    dy = (REAL)ry;
    if (dx > R - L) dx = R - L;
    if (dy > B - T) dy = B - T;
    if (dx <= 0.01f || dy <= 0.01f) {
        path.AddRectangle(RectF(L, T, R - L, B - T));
    } else {
        /* GDI 的 RoundRect，rx/ry 是圆角椭圆的直径 */
        path.AddArc(L, T, dx, dy, 180.0f, 90.0f);
        path.AddArc(R - dx, T, dx, dy, 270.0f, 90.0f);
        path.AddArc(R - dx, B - dy, dx, dy, 0.0f, 90.0f);
        path.AddArc(L, B - dy, dx, dy, 90.0f, 90.0f);
        path.CloseFigure();
    }
    StrokeFill(hdc, &path);
    return TRUE;
}

BOOL MerAAEllipseDC(HDC hdc, int l, int t, int r, int b) {
    GraphicsPath path;
    REAL rc[4];
    if (!hdc || !EnsureGA()) return FALSE;
    InsetRect(hdc, l, t, r, b, rc);
    path.AddEllipse(RectF(rc[0], rc[1], rc[2], rc[3]));
    StrokeFill(hdc, &path);
    return TRUE;
}

BOOL MerAAPieDC(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2) {
    GraphicsPath path;
    REAL rc[4];
    double cx, cy, a1, a2, sweep;
    if (!hdc || !EnsureGA()) return FALSE;
    InsetRect(hdc, l, t, r, b, rc);
    cx = (double)l + (double)(r - l) / 2.0;
    cy = (double)t + (double)(b - t) / 2.0;
    a1 = atan2((double)y1 - cy, (double)x1 - cx) * 180.0 / 3.14159265358979323846;
    a2 = atan2((double)y2 - cy, (double)x2 - cx) * 180.0 / 3.14159265358979323846;
    /* GDI 的 Pie 从第一点逆时针到第二点，GDI+ 的 sweep 正值是顺时针 */
    sweep = -(a2 - a1);
    while (sweep <= -360.0) sweep += 360.0;
    while (sweep > 0.0) sweep -= 360.0;
    path.AddPie(rc[0], rc[1], rc[2], rc[3], (REAL)a1, (REAL)sweep);
    StrokeFill(hdc, &path);
    return TRUE;
}

BOOL MerAAPolygonDC(HDC hdc, const POINT* pts, int n) {
    GraphicsPath path;
    PointF* buf;
    int i;
    if (!hdc || !pts || n < 3 || !EnsureGA()) return FALSE;
    buf = (PointF*)malloc(sizeof(PointF) * (size_t)n);
    if (!buf) return FALSE;
    /* 顶点是整数坐标，1px 笔心压上去正好落在像素中心，与 GDI 的 Polygon 覆盖一致，
       不需要像矩形那样内缩 */
    for (i = 0; i < n; i++) {
        buf[i].X = (REAL)pts[i].x;
        buf[i].Y = (REAL)pts[i].y;
    }
    path.AddPolygon(buf, n);
    free(buf);
    StrokeFill(hdc, &path);
    return TRUE;
}

/* 从 DC 当前 pen 造一支 GDI+ 笔，颜色/宽度/虚线全部沿用 */
static void MakePenFromDc(HDC hdc, Pen* pen) {
    DcStyle st;
    ReadDcStyle(hdc, &st);
    pen->SetColor(ToColor(st.penCol));
    pen->SetWidth((REAL)st.width);
    pen->SetLineJoin(LineJoinRound);
    if (st.dash) {
        REAL pat[2];
        pat[0] = 3.0f;
        pat[1] = 2.0f;
        pen->SetDashPattern(pat, 2);
    }
}

BOOL MerAAPolylineDC(HDC hdc, const POINT* pts, int n) {
    PointF* buf;
    int i;
    if (!hdc || !pts || n < 2 || !EnsureGA()) return FALSE;
    buf = (PointF*)malloc(sizeof(PointF) * (size_t)n);
    if (!buf) return FALSE;
    for (i = 0; i < n; i++) {
        buf[i].X = (REAL)pts[i].x;
        buf[i].Y = (REAL)pts[i].y;
    }
    {
        Graphics g(hdc);
        if (g.GetLastStatus() != Ok) { free(buf); return FALSE; }
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeNone);
        Pen pen(Color(255, 0, 0, 0), 1.0f);
        MakePenFromDc(hdc, &pen);
        if (g.DrawLines(&pen, buf, n) != Ok) { free(buf); return FALSE; }
    }
    free(buf);
    return TRUE;
}

BOOL MerAABezierDC(HDC hdc, const POINT* pts, int n) {
    GraphicsPath path;
    int i;
    if (!hdc || !pts || n < 4 || (n - 1) % 3 != 0 || !EnsureGA()) return FALSE;
    for (i = 0; i + 3 < n; i += 3) {
        path.AddBezier((REAL)pts[i].x, (REAL)pts[i].y,
                       (REAL)pts[i + 1].x, (REAL)pts[i + 1].y,
                       (REAL)pts[i + 2].x, (REAL)pts[i + 2].y,
                       (REAL)pts[i + 3].x, (REAL)pts[i + 3].y);
    }
    {
        Graphics g(hdc);
        if (g.GetLastStatus() != Ok) return FALSE;
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeNone);
        Pen pen(Color(255, 0, 0, 0), 1.0f);
        MakePenFromDc(hdc, &pen);
        if (g.DrawPath(&pen, &path) != Ok) return FALSE;
    }
    return TRUE;
}

BOOL MerAALineDC(HDC hdc, int x1, int y1, int x2, int y2) {
    POINT seg[2];
    seg[0].x = x1;
    seg[0].y = y1;
    seg[1].x = x2;
    seg[1].y = y2;
    return MerAAPolylineDC(hdc, seg, 2);
}

BOOL MerAAPolylineI(HDC hdc, const int (*pts)[2], int n, COLORREF col, int width, BOOL dash) {
    float* xy;
    int i;
    BOOL ok;
    if (!hdc || !pts || n < 2) return FALSE;
    xy = (float*)malloc(sizeof(float) * 2 * (size_t)n);
    if (!xy) return FALSE;
    for (i = 0; i < n; i++) {
        xy[i * 2] = (float)pts[i][0];
        xy[i * 2 + 1] = (float)pts[i][1];
    }
    ok = MerAAPolyline(hdc, xy, n, col, width, dash);
    free(xy);
    return ok;
}
