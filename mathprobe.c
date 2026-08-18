#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "../src/mdmath.c"

static int s_zoom = 100;
int UI_Scale(int px) { return MulDiv(px, s_zoom, 100); }

static int  (WINAPI* p_GdipStartup)(ULONG_PTR*, const void*, void*);
static void (WINAPI* p_GdipShutdown)(ULONG_PTR*);
static int  (WINAPI* p_GdipCreateBitmapFromHBITMAP)(HBITMAP, HPALETTE, void**);
static int  (WINAPI* p_GdipDisposeImage)(void*);
static int  (WINAPI* p_GdipSaveImageToFile)(void*, const WCHAR*, const GUID*, void*);

static HFONT Mk(int px, int weight, BOOL it, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, it, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static const char* KN(int k) {
    switch (k) {
        case MB_GLYPHS: return "GLYPHS";
        case MB_ROW: return "ROW";
        case MB_FRAC: return "FRAC";
        case MB_SCRIPT: return "SCRIPT";
        case MB_RADICAL: return "RADICAL";
        case MB_BIGOP: return "BIGOP";
    }
    return "?";
}

static void Dump(MathBox* b, int d) {
    if (!b) { fprintf(stderr, "%*s(null)\n", d * 2, ""); return; }
    fprintf(stderr, "%*s%s L%d w=%d h=%d asc=%d sp=%d kids=%d text=%ls\n", d * 2, "",
            KN(b->kind), b->level, b->w, b->h, b->asc, b->spaceAfter, b->nKids,
            b->kind == MB_GLYPHS ? b->text : L"-");
    for (int i = 0; i < b->nKids; i++) Dump(b->kids[i], d + 1);
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: mathprobe out.png zoom [formula]\n"); return 1; }
    {
        typedef LONG(WINAPI* SPDAC_t)(LONG);
        SPDAC_t fn = (SPDAC_t)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                             "SetProcessDpiAwarenessContext");
        if (fn) fn(-4);
    }
    s_zoom = atoi(argv[2]);
    if (s_zoom < 50) s_zoom *= 100;
    int W = 1600, H = 500;

    const char* src = argc > 3 ? argv[3]
        : "\\frac{a+b}{c} \\quad \\int_0^\\infty e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2} \\quad E=mc^2";
    int len = (int)strlen(src);
    wchar_t wsrc[2048];
    int wn = MultiByteToWideChar(CP_UTF8, 0, src, len, wsrc, 2047);
    wsrc[wn] = 0;

    MathBox* box = Math_Build(wsrc);
    if (!box) { printf("parse failed\n"); return 2; }

    MdFonts f;
    ZeroMemory(&f, sizeof(f));
    int px = MulDiv(11, s_zoom * 96 / 100, 72);
    f.body = Mk(px, FW_NORMAL, FALSE, L"Segoe UI");
    f.bold = Mk(px, FW_BOLD, FALSE, L"Segoe UI");
    f.sm = Mk(MulDiv(9, s_zoom * 96 / 100, 72), FW_NORMAL, FALSE, L"Segoe UI");
    f.mathIt = Mk(px, FW_NORMAL, TRUE, L"Cambria Math");
    f.mathUp = Mk(px, FW_NORMAL, FALSE, L"Cambria Math");
    f.mathItS = Mk((int)(px * 0.72 + 0.5), FW_NORMAL, TRUE, L"Cambria Math");
    f.mathUpS = Mk((int)(px * 0.72 + 0.5), FW_NORMAL, FALSE, L"Cambria Math");
    f.mathItSS = Mk((int)(px * 0.55 + 0.5), FW_NORMAL, TRUE, L"Cambria Math");
    f.mathUpSS = Mk((int)(px * 0.55 + 0.5), FW_NORMAL, FALSE, L"Cambria Math");

    MdTheme th;
    ZeroMemory(&th, sizeof(th));
    th.bg = RGB(255, 255, 255); th.fg = RGB(31, 35, 40);

    HDC scr = GetDC(NULL);
    HDC hdc = CreateCompatibleDC(scr);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(hdc, dib);

    RECT all = { 0, 0, W, H };
    HBRUSH bgb = CreateSolidBrush(th.bg);
    FillRect(hdc, &all, bgb);
    DeleteObject(bgb);

    {
        HFONT b = Mk(px, FW_NORMAL, FALSE, L"Segoe UI");
        HFONT ob2 = (HFONT)SelectObject(hdc, b);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, th.fg);
        SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
        TextOutW(hdc, 40, 60, L"text", 4);
        SelectObject(hdc, ob2);
        DeleteObject(b);
    }

    Math_Measure(box, hdc, &f);
    Dump(box, 0);
    {
        HFONT of = (HFONT)SelectObject(hdc, f.mathIt);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        SelectObject(hdc, of);
        fprintf(stderr, "[metric] mathIt px=%d tmH=%d asc=%d desc=%d intLead=%d extLead=%d\n",
                px, tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading);
        LOGFONTW lf; ZeroMemory(&lf, sizeof(lf));
        GetObjectW(f.mathIt, sizeof(lf), &lf);
        fprintf(stderr, "[font] face=%ls h=%ld weight=%d\n", lf.lfFaceName, lf.lfHeight, lf.lfWeight);
    }
    Math_Draw(box, hdc, 60, 200, &th, &f);
    GdiFlush();

    HMODULE gp = LoadLibraryW(L"gdiplus.dll");
    p_GdipStartup = (void*)GetProcAddress(gp, "GdiplusStartup");
    p_GdipShutdown = (void*)GetProcAddress(gp, "GdipShutdown");
    p_GdipCreateBitmapFromHBITMAP = (void*)GetProcAddress(gp, "GdipCreateBitmapFromHBITMAP");
    p_GdipDisposeImage = (void*)GetProcAddress(gp, "GdipDisposeImage");
    p_GdipSaveImageToFile = (void*)GetProcAddress(gp, "GdipSaveImageToFile");
    int ok = 0;
    if (p_GdipStartup && p_GdipCreateBitmapFromHBITMAP && p_GdipSaveImageToFile) {
        typedef struct { UINT32 v; void* Deb; BOOL sbt, sec; } SI;
        SI si; ZeroMemory(&si, sizeof(si)); si.v = 1;
        ULONG_PTR tok = 0;
        if (p_GdipStartup(&tok, &si, NULL) == 0) {
            void* img = NULL;
            if (p_GdipCreateBitmapFromHBITMAP(dib, NULL, &img) == 0 && img) {
                static const GUID pngEnc =
                    { 0x557CF406, 0x1A04, 0x11D3, { 0x9A,0x73,0x00,0x00,0xF8,0x1E,0xF3,0x2E } };
                wchar_t outw[MAX_PATH];
                MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, outw, MAX_PATH);
                if (p_GdipSaveImageToFile(img, outw, &pngEnc, NULL) == 0) ok = 1;
                p_GdipDisposeImage(img);
            }
            p_GdipShutdown(&tok);
        }
    }
    printf(ok ? "OK %s\n" : "SAVE FAIL %s\n", argv[1]);
    return ok ? 0 : 3;
}
