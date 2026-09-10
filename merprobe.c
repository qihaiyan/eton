/* merprobe.c — 本地验证工具：模拟 mdview 滚动视口中渲染 mermaid 图。
   用法：merprobe in.mmd out.png zoom docTop scrollY viewW viewH [--ddb|--win]
     zoom×100（175=175%）；--ddb 用 DDB 位图、--win 在真实窗口 DC 上绘制。
   构建（x64 Native Tools 下，在仓库根）：
     cl /nologo /W3 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
        /Isrc /Ideps\scintilla /Ideps\lexilla /Ideps\md4c ^
        /Febuild\merprobe.exe merprobe.c src\mermaid.c ^
        /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib kernel32.lib */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"

static int s_zoom = 100;   /* ×100：175 = 175% DPI */
int UI_Scale(int px) { return MulDiv(px, s_zoom, 100); }

static int  (WINAPI* p_GdipStartup)(ULONG_PTR*, const void*, void*);
static void (WINAPI* p_GdipShutdown)(ULONG_PTR*);
static int  (WINAPI* p_GdipCreateBitmapFromHBITMAP)(HBITMAP, HPALETTE, void**);
static int  (WINAPI* p_GdipDisposeImage)(void*);
static int  (WINAPI* p_GdipSaveImageToFile)(void*, const WCHAR*, const GUID*, void*);

static HFONT Mk(int px, int weight, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void ThemeDark(MdTheme* th) {
    th->bg = RGB(30,30,30);        th->fg = RGB(212,216,221);
    th->fgMuted = RGB(139,148,158); th->link = RGB(88,166,255);
    th->quoteBar = RGB(77,87,98);   th->codeBg = RGB(40,42,46);
    th->codeBorder = RGB(60,63,68); th->codeFg = RGB(230,220,200);
    th->tableLine = RGB(60,63,68);  th->tableHeadBg = RGB(44,46,51);
    th->hrule = RGB(60,63,68);      th->selBg = RGB(36,38,42);
    th->merNodeFill = RGB(47,52,60); th->merNodeBorder = RGB(110,120,135);
    th->merNodeText = RGB(220,224,228); th->merEdge = RGB(150,158,168);
    th->merLabelBg = RGB(30,30,30); th->merLabelFg = RGB(180,186,194);
    th->seqNoteBg = RGB(72,66,42);  th->seqNoteBorder = RGB(140,126,74);
    th->seqNoteFg = RGB(235,228,205); th->frame = RGB(96,104,116);
}

int main(int argc, char** argv) {
    if (argc < 8) { printf("usage: merprobe in.mmd out.png zoom docTop scrollY viewW viewH\n"); return 1; }
    /* 与 eton 一致：PerMonitorV2 DPI 感知（否则窗口/DC 被系统虚拟化，行为不同） */
    {
        typedef LONG (WINAPI* SPDAC_t)(LONG);
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        SPDAC_t spdac = (SPDAC_t)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (spdac) spdac(-4 /*PER_MONITOR_AWARE_V2*/);
    }
    s_zoom = atoi(argv[3]);
    if (s_zoom < 50) s_zoom *= 100;      /* 兼容传 1.75→atoi=1；显式传 175 */
    int docTop = atoi(argv[4]), scrollY = atoi(argv[5]);
    int W = atoi(argv[6]), H = atoi(argv[7]);

    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = (char*)malloc(n + 1);
    fread(buf, 1, n, fp);
    buf[n] = 0;
    fclose(fp);

    MermaidDiagram* d = Mermaid_Parse(buf, (int)n);
    free(buf);
    if (!d) { printf("parse failed\n"); return 2; }

    MdFonts f;
    ZeroMemory(&f, sizeof(f));
    /* 复刻 mdview：11pt 正文按 DPI 换算（MulDiv 同款整数运算），sm = body-2pt */
    int bodyPx = MulDiv(11, s_zoom * 96 / 100, 72);
    int smPx   = MulDiv(9,  s_zoom * 96 / 100, 72);
    f.body = Mk(bodyPx, FW_NORMAL, L"Segoe UI");
    f.bold = Mk(bodyPx, FW_BOLD,   L"Segoe UI");
    f.sm   = Mk(smPx,   FW_NORMAL, L"Segoe UI");
    f.mono = Mk(MulDiv(10, s_zoom * 96 / 100, 72), FW_NORMAL, L"Consolas");
    MdTheme th;
    ThemeDark(&th);

    HDC scr = GetDC(NULL);
    HDC hdc = CreateCompatibleDC(scr);

    /* --win 模式：在真实顶层窗口 DC 上执行 Measure+Draw（复刻 mdview 的绘制环境），
       完成后 BitBlt 回 DIB 保存。用法：merprobe in out zoom docTop scrollY W H --win */
    HWND hwndWin = NULL;
    HDC hdcWin = NULL;
    for (int ai = 8; ai < argc; ai++)
        if (strcmp(argv[ai], "--win") == 0) {
            WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = L"MerProbeWin";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RegisterClassW(&wc);
            hwndWin = CreateWindowExW(0, L"MerProbeWin", L"merprobe", WS_POPUP,
                                      3830 - W > 0 ? 3830 - W : 0, 100, W, H, NULL, NULL, wc.hInstance, NULL);
            ShowWindow(hwndWin, SW_SHOWNOACTIVATE);
            UpdateWindow(hwndWin);
            hdcWin = GetDC(hwndWin);
            RECT fll = { 0, 0, W, H };
            FillRect(hdcWin, &fll, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
    if (hdcWin) hdc = hdcWin;

    /* 字体度量自检：屏幕 DC（app 测量环境）vs DIB（app 绘制环境） */
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = NULL;
    /* --ddb：用 DDB（CreateCompatibleBitmap）而非 DIB section —— 复刻 mdview Paint 的内存位图 */
    int useDdb = 0;
    for (int ai = 8; ai < argc; ai++) if (strcmp(argv[ai], "--ddb") == 0) useDdb = 1;
    if (useDdb) dib = CreateCompatibleBitmap(scr, W, H);
    else dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    {
        HFONT of = (HFONT)SelectObject(hdc, f.body);
        SIZE sz;
        GetTextExtentPoint32W(hdc, L"多标签", 3, &sz);
        printf("bare-DC '多标签'=%d\n", sz.cx);
        HGDIOBJ ob2 = SelectObject(hdc, dib);
        GetTextExtentPoint32W(hdc, L"多标签", 3, &sz);
        printf("dib-DC '多标签'=%d\n", sz.cx);
        SelectObject(hdc, ob2);
        SelectObject(hdc, of);
        HFONT of2 = (HFONT)SelectObject(scr, f.body);
        GetTextExtentPoint32W(scr, L"多标签", 3, &sz);
        printf("screen-DC '多标签'=%d\n", sz.cx);
        SelectObject(scr, of2);
    }

    SIZE sz = Mermaid_Measure(d, hdc, &f);

    HGDIOBJ oldBmp = hdcWin ? NULL : SelectObject(hdc, dib);

    RECT rcAll = { 0, 0, W, H };
    HBRUSH bg = CreateSolidBrush(th.bg);
    FillRect(hdc, &rcAll, bg);
    DeleteObject(bg);

    /* 复刻 mdview：canvas 背景块 + 图表项（8px 内衬）+ 画布裁剪 */
    int x0 = hdcWin ? UI_Scale(60) : 60 * s_zoom;
    RECT canvas = { x0, docTop, x0 + sz.cx + UI_Scale(16), docTop + sz.cy + UI_Scale(16) };
    OffsetRect(&canvas, 0, -scrollY);
    HBRUSH cb = CreateSolidBrush(th.selBg);
    FillRect(hdc, &canvas, cb);
    DeleteObject(cb);

    /* 复刻 mdview：图表前先画标题（h[0] ≈ 11pt*1.9=21pt 大号粗体 Segoe UI，
       TextOutW per fragment）—— 前置字体实现可能影响后续 DrawText 光栅化 */
    {
        HFONT h0 = Mk(MulDiv(21, s_zoom * 96 / 100, 72), FW_BOLD, L"Segoe UI");
        HFONT oh = (HFONT)SelectObject(hdc, h0);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(31, 35, 40));
        TextOutW(hdc, x0 - UI_Scale(40), docTop, L"t 标题", 4);
        SelectObject(hdc, oh);
        DeleteObject(h0);
    }

    RECT drc = { x0 + UI_Scale(8), docTop + UI_Scale(8),
                 x0 + UI_Scale(8) + sz.cx, docTop + UI_Scale(8) + sz.cy };
    OffsetRect(&drc, 0, -scrollY);
    RECT clip = drc;
    InflateRect(&clip, UI_Scale(16), UI_Scale(16));
    /* 复刻 mdview：ITM_LINES 绘制后 DC 遗留 TA_LEFT|TA_BASELINE 文本对齐 */
    SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
    SaveDC(hdc);
    IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);
    Mermaid_Draw(d, hdc, drc.left, drc.top, &f, &th);
    RestoreDC(hdc, -1);

    /* 客观检测：用 MnDraw 同款字体/风格测 DrawTextW 实际需要的宽度 */
    {
        int padX = UI_Scale(12), padY = UI_Scale(6);
        const wchar_t* txts[] = { L"多标签", L"语法高亮", L"Markdown", L"编辑器" };
        static const char* tag[] = { "duoBiaoQian(3cjk)", "yuFaGaoLiang(4cjk)", "Markdown(ascii)", "bianJiQi(root-bold)" };
        for (int k = 0; k < 4; k++) {
            SIZE wsz;
            HFONT of = (HFONT)SelectObject(hdc, f.body);
            GetTextExtentPoint32W(hdc, txts[k], (int)wcslen(txts[k]), &wsz);
            int w = wsz.cx + padX * 2, h = 36 + padY * 2;
            RECT rc = { 0, 0, w - UI_Scale(4) * 2, h - UI_Scale(2) * 2 };
            RECT calc = rc;
            HFONT ofB = (HFONT)SelectObject(hdc, k == 3 ? f.bold : f.body);
            DrawTextW(hdc, txts[k], -1, &calc, DT_CALCRECT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, of);
            SelectObject(hdc, ofB);
            printf("%s GTEP=%d nodeW=%d rectW=%d DTneed=%d %s\n",
                   tag[k], wsz.cx, w, rc.right, calc.right,
                   calc.right > rc.right ? "TRUNC" : "fits");
        }
    }
    GdiFlush();
    if (hdcWin) {   /* --win：把窗口 DC 上的绘制结果拷回 DIB 以便保存 */
        HDC mdc = CreateCompatibleDC(hdcWin);
        HGDIOBJ ob = SelectObject(mdc, dib);
        BitBlt(mdc, 0, 0, W, H, hdcWin, 0, 0, SRCCOPY);
        SelectObject(mdc, ob);
        DeleteDC(mdc);
        GdiFlush();
        Sleep(80);   /* 窗口稍作停留便于肉眼/截图确认 */
    }

    HMODULE gp = LoadLibraryW(L"gdiplus.dll");
    p_GdipStartup = (void*)GetProcAddress(gp, "GdiplusStartup");
    p_GdipShutdown = (void*)GetProcAddress(gp, "GdiplusShutdown");
    p_GdipCreateBitmapFromHBITMAP = (void*)GetProcAddress(gp, "GdipCreateBitmapFromHBITMAP");
    p_GdipDisposeImage = (void*)GetProcAddress(gp, "GdipDisposeImage");
    p_GdipSaveImageToFile = (void*)GetProcAddress(gp, "GdipSaveImageToFile");
    int ok = 0;
    if (p_GdipStartup && p_GdipCreateBitmapFromHBITMAP && p_GdipSaveImageToFile) {
        typedef struct { UINT32 v; void* Deb; BOOL sbt, sec; } SI;
        SI si; ZeroMemory(&si, sizeof(si));
        si.v = 1;
        ULONG_PTR tok = 0;
        if (p_GdipStartup(&tok, &si, NULL) == 0) {
            void* img = NULL;
            if (p_GdipCreateBitmapFromHBITMAP(dib, NULL, &img) == 0 && img) {
                static const GUID pngEnc =
                    { 0x557CF406, 0x1A04, 0x11D3, { 0x9A,0x73,0x00,0x00,0xF8,0x1E,0xF3,0x2E } };
                wchar_t outw[MAX_PATH];
                MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, outw, MAX_PATH);
                if (p_GdipSaveImageToFile(img, outw, &pngEnc, NULL) == 0) ok = 1;
                p_GdipDisposeImage(img);
            }
            p_GdipShutdown(&tok);
        }
    }

    SelectObject(hdc, oldBmp);
    DeleteObject(dib);
    DeleteDC(hdc);
    ReleaseDC(NULL, scr);
    DeleteObject(f.body); DeleteObject(f.bold); DeleteObject(f.sm); DeleteObject(f.mono);
    Mermaid_Free(d);
    printf(ok ? "OK %s\n" : "SAVE FAIL %s\n", argv[2]);
    return ok ? 0 : 3;
}
