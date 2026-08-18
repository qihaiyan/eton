#ifndef MERAA_H
#define MERAA_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 显式指定颜色的图元 ---------- */

/* 抗锯齿折线。GDI 的 LineTo / PolyBezier 不做抗锯齿，斜线会有明显阶梯。
   这里用 GDI+ 画，保留亚像素精度。xy 为交错坐标 {x0,y0,x1,y1,...}，
   n 是点的个数，坐标直接传当前 GDI 逻辑坐标即可（GDI+ 会自己带上 DC 的
   viewport origin，别手动加偏移）。返回 TRUE 表示已绘制，FALSE 表示 GDI+
   不可用，调用方应自行用 GDI 兜底。 */
BOOL MerAAPolyline(HDC hdc, const float* xy, int n, COLORREF col, int width, BOOL dash);

/* 抗锯齿多边形（箭头、菱形端头等）。仅描边，不填充。 */
BOOL MerAAPolygon(HDC hdc, const float* xy, int n, COLORREF col, BOOL filled);

/* ---------- 按 DC 当前 pen / brush 绘制的图元 ----------
 *
 * 语义与同名 GDI 函数对齐（Rectangle / RoundRect / Ellipse / Pie / Polygon），
 * 包括「1px 描边压在矩形内侧」这一点：GDI 的 Rectangle 边框占据 x=l 与 x=r-1
 * 两列像素，而 GDI+ 的 DrawPath 是笔中心压在路径上，所以内部统一把路径往内
 * 收半像素，保证换过去以后节点框不偏移、不加粗。
 *
 * 虚线与填充都从 DC 取：pen 的 PS_DASH → GDI+ 虚线；brush 为 NULL_BRUSH 则只
 * 描边，为实心则填充。
 *
 * 返回 FALSE 表示 GDI+ 不可用，调用方应用原 GDI 函数兜底。
 */
BOOL MerAARectDC(HDC hdc, int l, int t, int r, int b);
BOOL MerAARoundRectDC(HDC hdc, int l, int t, int r, int b, int rx, int ry);
BOOL MerAAEllipseDC(HDC hdc, int l, int t, int r, int b);
BOOL MerAAPieDC(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2);
BOOL MerAAPolygonDC(HDC hdc, const POINT* pts, int n);

/* 折线（不闭合）与三次贝塞尔，同样取 DC 当前的 pen。
   贝塞尔要求 n == 1 + 3k，与 GDI 的 PolyBezier 一致。 */
BOOL MerAAPolylineDC(HDC hdc, const POINT* pts, int n);
BOOL MerAABezierDC(HDC hdc, const POINT* pts, int n);

/* 单段直线，等价于 MoveToEx + LineTo。 */
BOOL MerAALineDC(HDC hdc, int x1, int y1, int x2, int y2);

/* 把一串整数点转成抗锯齿点列后绘制，供旧走线分支（CurveSeg 之类）复用。 */
BOOL MerAAPolylineI(HDC hdc, const int (*pts)[2], int n, COLORREF col, int width, BOOL dash);

#ifdef __cplusplus
}
#endif

#endif
