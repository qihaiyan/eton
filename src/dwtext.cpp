// DirectWrite text engine for the Markdown preview. Compiled as C++ because
// the SDK dwrite.h declares its interfaces in C++-only syntax; everything
// exported through dwtext.h stays plain C.
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "dwtext.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <wincodec.h>
#include <objbase.h>
#pragma comment(lib, "msimg32.lib")

struct DwtFmt {
    IDWriteTextFormat* fmt;
    short* cw;
    int lineH, asc;
    wchar_t face[64];
    int px, weight;
    BOOL italic;
};

static ID2D1Factory*         s_factory;
static IDWriteFactory*       s_dw;
static IWICImagingFactory*   s_wic;
static IWICBitmap*           s_wib;
static ID2D1RenderTarget*    s_rt;
static ID2D1SolidColorBrush* s_brush;
static int s_rtW, s_rtH;
static HDC s_dibDC; static HBITMAP s_dib, s_dibOld;
static int s_dibW, s_dibH;
static BOOL s_ok, s_tried;

static DwtFmt s_fmts[24];
static int s_nFmts;

struct DwtTextQ { DwtFmt* f; int x, y, w; const wchar_t* s; int len; COLORREF c; };
struct DwtLineQ { int x, y, w; COLORREF c; };
static DwtTextQ* s_tq; static int s_nT, s_capT;
static DwtLineQ* s_lq;  static int s_nL, s_capL;

static BOOL DwtInit() {
    if (s_tried) return s_ok;
    s_tried = TRUE;
    HMODULE hd2d = LoadLibraryW(L"d2d1.dll");
    HMODULE hdwr = LoadLibraryW(L"dwrite.dll");
    if (!hd2d || !hdwr) return FALSE;
    typedef HRESULT (WINAPI *PFN_D2D1CreateFactory)(D2D1_FACTORY_TYPE, REFIID,
                                                    const D2D1_FACTORY_OPTIONS*, void**);
    typedef HRESULT (WINAPI *PFN_DWriteCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    PFN_D2D1CreateFactory pD2DCF =
        (PFN_D2D1CreateFactory)(void*)GetProcAddress(hd2d, "D2D1CreateFactory");
    PFN_DWriteCreateFactory pDWCF =
        (PFN_DWriteCreateFactory)(void*)GetProcAddress(hdwr, "DWriteCreateFactory");
    if (!pD2DCF || !pDWCF) return FALSE;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    static const CLSID k_wicFactory =
        { 0xcacaf262, 0x9370, 0x4615,
          { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
    if (FAILED(CoCreateInstance(k_wicFactory, NULL,
                                CLSCTX_INPROC_SERVER,
                                __uuidof(IWICImagingFactory), (void**)&s_wic)))
        return FALSE;
    if (FAILED(pDWCF(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                     (IUnknown**)&s_dw))) return FALSE;
    if (FAILED(pD2DCF(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                      NULL, (void**)&s_factory))) return FALSE;
    s_ok = TRUE;
    return TRUE;
}

// Render text into a private WIC bitmap (premultiplied BGRA) and composite it
// with a hand-driven AlphaBlend. A DC render target's EndDraw composites its
// own way and was observed filling the whole bound rect opaque black on some
// paths, so every step of the transfer is kept under our control here.
static BOOL DwtEnsureTarget(int w, int h) {
    if (s_rt && s_rtW == w && s_rtH == h) return TRUE;
    if (s_brush) { s_brush->Release(); s_brush = NULL; }
    if (s_rt) { s_rt->Release(); s_rt = NULL; }
    if (s_wib) { s_wib->Release(); s_wib = NULL; }
    if (w <= 0 || h <= 0) return FALSE;
    if (FAILED(s_wic->CreateBitmap((UINT)w, (UINT)h,
                                   GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapCacheOnLoad, &s_wib)) || !s_wib)
        return FALSE;
    D2D1_RENDER_TARGET_PROPERTIES props;
    ZeroMemory(&props, sizeof(props));
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    if (FAILED(s_factory->CreateWicBitmapRenderTarget(s_wib, props, &s_rt)) || !s_rt)
        return FALSE;
    // Bitmap targets have no subpixel geometry; grayscale AA is the only valid
    // mode (and matches Chromium/Feishu on Windows).
    s_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(s_rt->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &s_brush)))
        return FALSE;
    s_rtW = w; s_rtH = h;
    return TRUE;
}

extern "C" BOOL Dwt_Ok() {
    return DwtInit();
}

// Custom fallback: CJK han ranges -> SimHei (its glyphs keep every stroke
// inside the character box, unlike Microsoft YaHei's protruding 类 dot);
// all other scripts keep the system fallback via AddMappings.
static IDWriteFontFallback* DwtBuildFallback() {
    IDWriteFactory2* f2 = NULL;
    if (FAILED(s_dw->QueryInterface(__uuidof(IDWriteFactory2), (void**)&f2)) || !f2)
        return NULL;
    IDWriteFontFallbackBuilder* b = NULL;
    IDWriteFontFallback* sys = NULL;
    IDWriteFontFallback* fb = NULL;
    HRESULT hr = f2->CreateFontFallbackBuilder(&b);
    if (SUCCEEDED(hr) && b) {
        static const DWRITE_UNICODE_RANGE cjk[] = {
            { 0x2E80, 0x2EFF },   // CJK radicals
            { 0x3000, 0x303F },   // CJK symbols & punctuation
            { 0x31C0, 0x31EF },   // CJK strokes
            { 0x3400, 0x4DBF },   // CJK ext A
            { 0x4E00, 0x9FFF },   // CJK unified ideographs
            { 0xF900, 0xFAFF },   // CJK compatibility ideographs
            { 0x20000, 0x2A6DF }, // CJK ext B
        };
        const wchar_t* heit = L"SimHei";
        b->AddMapping(cjk, (UINT32)(sizeof(cjk) / sizeof(cjk[0])),
                      &heit, 1, NULL, NULL, NULL, 1.0f);
        if (SUCCEEDED(f2->GetSystemFontFallback(&sys)) && sys)
            b->AddMappings(sys);
        if (FAILED(b->CreateFontFallback(&fb)))
            fb = NULL;
        if (sys) sys->Release();
        b->Release();
    }
    f2->Release();
    return fb;
}

extern "C" DwtFmt* Dwt_Font(const wchar_t* face, int px, int weight, BOOL italic,
                            int lineH, int asc) {
    if (!DwtInit() || !face) return NULL;
    for (int i = 0; i < s_nFmts; i++)
        if (s_fmts[i].px == px && s_fmts[i].weight == weight &&
            s_fmts[i].italic == italic && wcscmp(s_fmts[i].face, face) == 0)
            return &s_fmts[i];
    if (s_nFmts >= (int)(sizeof(s_fmts) / sizeof(s_fmts[0]))) return NULL;
    DwtFmt* f = &s_fmts[s_nFmts];
    ZeroMemory(f, sizeof(*f));
    if (px < 4) px = 4;
    if (FAILED(s_dw->CreateTextFormat(
            face, NULL, (DWRITE_FONT_WEIGHT)weight,
            italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, (FLOAT)px, L"", &f->fmt)))
        return NULL;
    f->fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    f->fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                           (FLOAT)lineH, (FLOAT)asc);
    {
        IDWriteTextFormat1* f1 = NULL;
        if (SUCCEEDED(f->fmt->QueryInterface(__uuidof(IDWriteTextFormat1),
                                             (void**)&f1)) && f1) {
            IDWriteFontFallback* fb = DwtBuildFallback();
            if (fb) {
                f1->SetFontFallback(fb);
                fb->Release();
            }
            f1->Release();
        }
    }
    f->lineH = lineH; f->asc = asc;
    f->px = px; f->weight = weight; f->italic = italic;
    wcsncpy(f->face, face, 63); f->face[63] = L'\0';
    s_nFmts++;
    return f;
}

extern "C" void Dwt_FontsChanged() {
    for (int i = 0; i < s_nFmts; i++) {
        if (s_fmts[i].fmt) s_fmts[i].fmt->Release();
        free(s_fmts[i].cw);
    }
    ZeroMemory(s_fmts, sizeof(s_fmts));
    s_nFmts = 0;
}

static int DwtWidthRaw(DwtFmt* f, const wchar_t* s, int len) {
    IDWriteTextLayout* tl = NULL;
    if (FAILED(s_dw->CreateTextLayout(s, (UINT32)len, f->fmt,
                                      1000000.0f, 1000.0f, &tl)) || !tl)
        return -1;
    DWRITE_TEXT_METRICS m;
    tl->GetMetrics(&m);
    tl->Release();
    return (int)(m.widthIncludingTrailingWhitespace + 0.5f);
}

extern "C" int Dwt_Width(DwtFmt* f, const wchar_t* s, int len) {
    if (!f || !s || len <= 0) return 0;
    if (len == 1) {
        if (!f->cw) {
            f->cw = (short*)malloc(sizeof(short) * 65536);
            if (f->cw) memset(f->cw, 0, sizeof(short) * 65536);
        }
        if (f->cw) {
            unsigned ch = (unsigned)s[0];
            if (f->cw[ch]) return f->cw[ch] - 1;
            int w = DwtWidthRaw(f, s, 1);
            if (w >= 0) f->cw[ch] = (short)(w + 1);
            return w;
        }
    }
    return DwtWidthRaw(f, s, len);
}

extern "C" void Dwt_Text(DwtFmt* f, int x, int baseY, const wchar_t* s, int len,
                         int w, COLORREF c) {
    if (!s_ok || !f || !s || len <= 0) return;
    if (s_nT >= s_capT) {
        int nc = s_capT ? s_capT * 2 : 256;
        DwtTextQ* nq = (DwtTextQ*)realloc(s_tq, sizeof(DwtTextQ) * nc);
        if (!nq) return;
        s_tq = nq; s_capT = nc;
    }
    s_tq[s_nT].f = f; s_tq[s_nT].x = x; s_tq[s_nT].y = baseY;
    s_tq[s_nT].w = w; s_tq[s_nT].s = s; s_tq[s_nT].len = len;
    s_tq[s_nT].c = c;
    s_nT++;
}

extern "C" void Dwt_Line(int x, int y, int w, COLORREF c) {
    if (!s_ok) return;
    if (s_nL >= s_capL) {
        int nc = s_capL ? s_capL * 2 : 64;
        DwtLineQ* nq = (DwtLineQ*)realloc(s_lq, sizeof(DwtLineQ) * nc);
        if (!nq) return;
        s_lq = nq; s_capL = nc;
    }
    s_lq[s_nL].x = x; s_lq[s_nL].y = y; s_lq[s_nL].w = w; s_lq[s_nL].c = c;
    s_nL++;
}

static BOOL DwtEnsureDib(int w, int h) {
    if (s_dibDC && s_dibW == w && s_dibH == h) return TRUE;
    if (s_dibDC) {
        SelectObject(s_dibDC, s_dibOld);
        DeleteObject(s_dib);
        DeleteDC(s_dibDC);
        s_dibDC = NULL; s_dib = NULL;
    }
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pv = NULL;
    HDC src = GetDC(NULL);
    s_dibDC = CreateCompatibleDC(src);
    ReleaseDC(NULL, src);
    if (!s_dibDC) return FALSE;
    s_dib = CreateDIBSection(s_dibDC, &bi, DIB_RGB_COLORS, &pv, NULL, 0);
    if (!s_dib) {
        DeleteDC(s_dibDC); s_dibDC = NULL;
        return FALSE;
    }
    s_dibOld = (HBITMAP)SelectObject(s_dibDC, s_dib);
    s_dibW = w; s_dibH = h;
    return TRUE;
}

extern "C" void Dwt_Flush(HDC hdc, const RECT* rc) {
    if (!s_ok || (!s_nT && !s_nL)) return;
    RECT full;
    if (!rc) {
        full.left = 0; full.top = 0;
        full.right = GetDeviceCaps(hdc, HORZRES);
        full.bottom = GetDeviceCaps(hdc, VERTRES);
        rc = &full;
    }
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    if (s_nT && w > 0 && h > 0 && DwtEnsureTarget(w, h) && DwtEnsureDib(w, h)) {
        s_rt->BeginDraw();
        s_rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        for (int i = 0; i < s_nT; i++) {
            const DwtTextQ* t = &s_tq[i];
            s_brush->SetColor(D2D1::ColorF((FLOAT)GetRValue(t->c) / 255.0f,
                                           (FLOAT)GetGValue(t->c) / 255.0f,
                                           (FLOAT)GetBValue(t->c) / 255.0f, 1.0f));
            D2D1_RECT_F r;
            r.left = (FLOAT)t->x;
            r.top = (FLOAT)(t->y - t->f->asc);
            r.right = (FLOAT)(t->x + t->w + 64.0f);
            r.bottom = (FLOAT)(t->y - t->f->asc + t->f->lineH);
            s_rt->DrawTextW(t->s, (UINT32)t->len, t->f->fmt, r, s_brush,
                            D2D1_DRAW_TEXT_OPTIONS_NONE,
                            DWRITE_MEASURING_MODE_NATURAL);
        }
        if (SUCCEEDED(s_rt->EndDraw(NULL, NULL))) {
            IWICBitmapLock* lock = NULL;
            if (SUCCEEDED(s_wib->Lock(NULL, WICBitmapLockRead, &lock)) && lock) {
                UINT stride = 0, size = 0;
                BYTE* data = NULL;
                lock->GetStride(&stride);
                lock->GetDataPointer(&size, &data);
                BITMAPINFO bi;
                ZeroMemory(&bi, sizeof(bi));
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                if (data && stride == (UINT)(w * 4)) {
                    SetDIBits(s_dibDC, s_dib, 0, (UINT)h, data, &bi, DIB_RGB_COLORS);
                    BLENDFUNCTION bf;
                    bf.BlendOp = AC_SRC_OVER;
                    bf.BlendFlags = 0;
                    bf.SourceConstantAlpha = 255;
                    bf.AlphaFormat = AC_SRC_ALPHA;
                    AlphaBlend(hdc, rc->left, rc->top, w, h,
                               s_dibDC, 0, 0, w, h, bf);
                }
                lock->Release();
            }
        }
    }
    for (int i = 0; i < s_nL; i++) {
        HPEN p = CreatePen(PS_SOLID, 1, s_lq[i].c);
        if (!p) continue;
        HPEN op = (HPEN)SelectObject(hdc, p);
        MoveToEx(hdc, s_lq[i].x, s_lq[i].y, NULL);
        LineTo(hdc, s_lq[i].x + s_lq[i].w, s_lq[i].y);
        SelectObject(hdc, op);
        DeleteObject(p);
    }
    s_nT = 0; s_nL = 0;
}
