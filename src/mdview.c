#include "common.h"
#include "md4c.h"
#include "nanosvg.h"
#include "nanosvgrast.h"

typedef enum { ITM_LINES, ITM_RECT, ITM_FRAME, ITM_DIAGRAM, ITM_IMAGE, ITM_MATH } MdItemType;
enum { RCT_CODEBG = 0, RCT_QUOTEBAR, RCT_HRULE, RCT_TABLEHEAD, RCT_CANVAS };

typedef struct MdFrag {
    MdRun* run;
    const wchar_t* s; int len;
    int x, w;
} MdFrag;

typedef struct MdLine {
    int y, h, asc;
    MdFrag* frags; int nF;
} MdLine;

typedef struct MdItem {
    int type;
    RECT rc;
    int flag;
    MdLine* lines; int nLines;
    int role;
    MermaidDiagram* diag;
    void* image;
    UINT imageW, imageH;
    void* thumb;            /* 按显示尺寸预渲染的位图（滚动时免逐帧重采样） */
    UINT thumbW, thumbH;
    MdRun* mathRun;
    MdRun* ownRun;
    wchar_t* href;      /* 图片项的点击目标（来自包裹链接或图片源） */
} MdItem;

/* ---- 文本选区 ----
   位置 = 项/行/片段/字符 的字典序；跨项拖选即比较两端大小。
   布局只会向后追加项，既有索引稳定；重排（ItemResetAll）时选区一并复位。 */
typedef struct { int item, line, frag, ch; } MdPos;

static int PosCmp(MdPos a, MdPos b) {
    if (a.item != b.item) return a.item < b.item ? -1 : 1;
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    if (a.frag != b.frag) return a.frag < b.frag ? -1 : 1;
    if (a.ch != b.ch) return a.ch < b.ch ? -1 : 1;
    return 0;
}

static struct {
    HWND hwnd;
    MdBlock* root;
    int docIdx;
    unsigned modGen;
    MdItem* items; int nItems, capItems;
    int docH, scrollY, clientW, clientH;
    MdFonts fonts;
    BOOL fontsOk;
    wchar_t baseDir[MAX_PATH];
    HCURSOR hHand;
    BOOL sbDrag; int sbDragOff;
    int lastLayoutW;
    BOOL imgTimerOn;     /* 图片到达合并窗口已挂起 */
    int layIdx, layY;    /* 增量布局：下一个顶层块索引 / 已布局到的 y */
    BOOL layDone;
    BOOL selActive;      /* 存在选区锚点 */
    BOOL selDrag;        /* 正在拖选 */
    MdPos selAnchor, selCaret;
    wchar_t* pendLink;   /* 按下点的链接：未拖开时抬起才打开 */
    POINT downPt;
} V;

#define TID_MDIMG 2         /* WM_MDIMG_READY 的 120ms 合并定时器 */

/* 重排会重建 items，选区索引随之失效——统一在此复位 */
static void SelReset(void) {
    V.selActive = FALSE;
    V.selDrag = FALSE;
    if (V.pendLink) { free(V.pendLink); V.pendLink = NULL; }
    ZeroMemory(&V.selAnchor, sizeof(MdPos));
    ZeroMemory(&V.selCaret, sizeof(MdPos));
}

static BOOL SelHasText(void);
static BOOL SelPointCovered(int item, int line, int frag);
static void SelFragRange(int item, int line, int frag, int len, int* a, int* b);

typedef void* GpImage;
typedef void* GpGraphics;
typedef struct { UINT32 GdiplusVersion; void* Deb; BOOL sbt, sec; } GdipStartupInput;
static int  (WINAPI* p_GdipStartup)(ULONG_PTR*, const GdipStartupInput*, void*);
static void (WINAPI* p_GdipShutdown)(ULONG_PTR);
static int  (WINAPI* p_GdipCreateBitmapFromFile)(const WCHAR*, GpImage**);
static int  (WINAPI* p_GdipDisposeImage)(GpImage*);
static int  (WINAPI* p_GdipGetImageWidth)(GpImage*, UINT*);
static int  (WINAPI* p_GdipGetImageHeight)(GpImage*, UINT*);
static int  (WINAPI* p_GdipCreateFromHDC)(HDC, GpGraphics**);
static int  (WINAPI* p_GdipDeleteGraphics)(GpGraphics*);
static int  (WINAPI* p_GdipSetInterpolationMode)(GpGraphics*, int);
static int  (WINAPI* p_GdipDrawImageRectRectI)(GpGraphics*, GpImage*,
                    int, int, int, int, int, int, int, int, int, void*, void*, void*);
static int  (WINAPI* p_GdipFlush)(GpGraphics*, int);
static int  (WINAPI* p_GdipCreateBitmapFromGraphics)(int, int, GpGraphics*, GpImage**);
static int  (WINAPI* p_GdipGetImageGraphicsContext)(GpImage*, GpGraphics**);
static int  (WINAPI* p_GdipCreateBitmapFromScan0)(int, int, int, int, void*, GpImage**);
static BOOL s_gdipOk = FALSE;

static void GdipInit(void) {
    HMODULE g = LoadLibraryW(L"gdiplus.dll");
    if (!g) return;
    p_GdipStartup = (void*)GetProcAddress(g, "GdiplusStartup");
    p_GdipShutdown = (void*)GetProcAddress(g, "GdiplusShutdown");
    p_GdipCreateBitmapFromFile = (void*)GetProcAddress(g, "GdipCreateBitmapFromFile");
    p_GdipDisposeImage = (void*)GetProcAddress(g, "GdipDisposeImage");
    p_GdipGetImageWidth = (void*)GetProcAddress(g, "GdipGetImageWidth");
    p_GdipGetImageHeight = (void*)GetProcAddress(g, "GdipGetImageHeight");
    p_GdipCreateFromHDC = (void*)GetProcAddress(g, "GdipCreateFromHDC");
    p_GdipDeleteGraphics = (void*)GetProcAddress(g, "GdipDeleteGraphics");
    p_GdipSetInterpolationMode = (void*)GetProcAddress(g, "GdipSetInterpolationMode");
    p_GdipDrawImageRectRectI = (void*)GetProcAddress(g, "GdipDrawImageRectRectI");
    p_GdipFlush = (void*)GetProcAddress(g, "GdipFlush");
    p_GdipCreateBitmapFromGraphics = (void*)GetProcAddress(g, "GdipCreateBitmapFromGraphics");
    p_GdipGetImageGraphicsContext = (void*)GetProcAddress(g, "GdipGetImageGraphicsContext");
    p_GdipCreateBitmapFromScan0 = (void*)GetProcAddress(g, "GdipCreateBitmapFromScan0");
    if (!p_GdipStartup || !p_GdipCreateBitmapFromFile) return;
    GdipStartupInput si;
    ZeroMemory(&si, sizeof(si));
    si.GdiplusVersion = 1;
    ULONG_PTR tok = 0;
    if (p_GdipStartup(&tok, &si, NULL) == 0) s_gdipOk = TRUE;
}

typedef struct { wchar_t path[MAX_PATH]; GpImage* img; UINT w, h; unsigned gen;
                 void* px; } ImgEnt;   /* px：SVG 光栅缓冲（位图直接引用，随槽位释放） */
static ImgEnt s_imgs[16];
static int s_nImgs = 0;
static unsigned s_imgGen = 0;

static ImgEnt* ImgFind(const wchar_t* full) {
    for (int i = 0; i < s_nImgs; i++)
        if (_wcsicmp(s_imgs[i].path, full) == 0) return &s_imgs[i];
    return NULL;
}

/* SVG → 位图：nanosvg 解析 + 2 倍光栅（位图细节高于逻辑尺寸，缩放显示更清晰）。
 * GdipCreateBitmapFromScan0 不拷贝扫描线，缓冲由 ImgEnt.px 持有。 */
static GpImage* LoadSvgBitmap(const wchar_t* full, UINT* ow, UINT* oh, void** pxOut) {
    *pxOut = NULL;
    HANDLE f = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(f); return NULL;
    }
    size_t n = (size_t)sz.QuadPart;
    char* buf = (char*)malloc(n + 1);
    if (!buf) { CloseHandle(f); return NULL; }
    DWORD rd = 0;
    BOOL ok = ReadFile(f, buf, (DWORD)n, &rd, NULL) && rd == (DWORD)n;
    CloseHandle(f);
    if (!ok) { free(buf); return NULL; }
    buf[n] = 0;

    NSVGimage* img = nsvgParse(buf, "px", 96.0f);
    free(buf);
    if (!img || img->width <= 0 || img->height <= 0) {
        if (img) nsvgDelete(img);
        return NULL;
    }
    UINT lw = (UINT)(img->width + 0.5f), lh = (UINT)(img->height + 0.5f);
    if (!lw || !lh || lw > 4096 || lh > 4096) { nsvgDelete(img); return NULL; }
    float scale = 2.0f;
    if (lw * scale > 4096) scale = 4096.0f / lw;
    if (lh * scale > 4096 && 4096.0f / lh < scale) scale = 4096.0f / lh;
    UINT rw = (UINT)(img->width * scale + 0.5f);
    UINT rh = (UINT)(img->height * scale + 0.5f);
    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;
    unsigned char* px = (unsigned char*)malloc((size_t)rw * rh * 4);
    NSVGrasterizer* rast = px ? nsvgCreateRasterizer() : NULL;
    if (!px || !rast) { free(px); if (rast) nsvgDeleteRasterizer(rast); nsvgDelete(img); return NULL; }
    nsvgRasterize(rast, img, 0, 0, scale, px, (int)rw, (int)rh, (int)rw * 4);
    nsvgDeleteRasterizer(rast);
    /* nanosvg 输出按内存 R,G,B,A；GDI+ 32bppARGB 的内存序是 B,G,R,A —— 原地换 R/B */
    for (unsigned char* p = px, *end = px + (size_t)rw * rh * 4; p < end; p += 4) {
        unsigned char t = p[0];
        p[0] = p[2];
        p[2] = t;
    }

    GpImage* bmp = NULL;
    /* PixelFormat32bppARGB */
    if (!p_GdipCreateBitmapFromScan0 || p_GdipCreateBitmapFromScan0((int)rw, (int)rh,
            (int)rw * 4, 0x26200A, px, &bmp) != 0 || !bmp) {
        free(px);
        nsvgDelete(img);
        return NULL;
    }
    nsvgDelete(img);
    *pxOut = px;
    *ow = lw;
    *oh = lh;
    return bmp;
}

static ImgEnt* ImgGet(const wchar_t* full) {
    if (!s_gdipOk || !full || !full[0]) return NULL;
    ImgEnt* hit = ImgFind(full);
    if (hit) { hit->gen = s_imgGen; return hit->img ? hit : NULL; }
    GpImage* im = NULL;
    UINT w = 0, h = 0;
    void* svgPx = NULL;
    size_t fl = wcslen(full);
    BOOL isSvg = (fl > 4 && _wcsicmp(full + fl - 4, L".svg") == 0);
    if (isSvg)
        im = LoadSvgBitmap(full, &w, &h, &svgPx);
    else {
        if (p_GdipCreateBitmapFromFile(full, &im) != 0 || !im) return NULL;
        p_GdipGetImageWidth(im, &w);
        p_GdipGetImageHeight(im, &h);
    }
    if (!im || !w || !h) {
        if (im) p_GdipDisposeImage(im);
        free(svgPx);
        return NULL;
    }
    /* 满时淘汰最久未使用（gen 最小）的槽位，而不是固定挤掉 0 号 */
    int slot = 0;
    if (s_nImgs < 16) slot = s_nImgs++;
    else {
        for (int i = 1; i < 16; i++) if (s_imgs[i].gen < s_imgs[slot].gen) slot = i;
        if (s_imgs[slot].img) p_GdipDisposeImage(s_imgs[slot].img);
        free(s_imgs[slot].px);
    }
    wcscpy_s(s_imgs[slot].path, MAX_PATH, full);
    s_imgs[slot].img = im;
    s_imgs[slot].w = w;
    s_imgs[slot].h = h;
    s_imgs[slot].gen = s_imgGen;
    s_imgs[slot].px = svgPx;
    return &s_imgs[slot];
}

/* 释放本次布局未引用的位图（换文档/内容变更后） */
static void ImgSweep(void) {
    int keep = 0;
    for (int i = 0; i < s_nImgs; i++) {
        if (s_imgs[i].gen == s_imgGen) s_imgs[keep++] = s_imgs[i];
        else {
            if (s_imgs[i].img) p_GdipDisposeImage(s_imgs[i].img);
            free(s_imgs[i].px);
        }
    }
    s_nImgs = keep;
}

static MdBlock* BlkNew(int type) {
    MdBlock* b = (MdBlock*)calloc(1, sizeof(MdBlock));
    if (b) b->type = type;
    return b;
}

#define MD_GROW(arr, need, cap, type) do { \
    if ((need) >= (cap)) { \
        int nc_ = (cap) ? (cap) * 2 : 16; \
        while (nc_ <= (need)) nc_ *= 2; \
        (arr) = (type*)realloc((arr), (size_t)nc_ * sizeof(type)); \
        (cap) = nc_; \
    } \
} while (0)

static void BlkAppend(MdBlock* parent, MdBlock* child) {
    MD_GROW(parent->children, parent->nChildren, parent->capChildren, MdBlock*);
    if (!parent->children) return;
    parent->children[parent->nChildren++] = child;
}

/* ---- 图表/公式解析缓存 ----
   LoadContentEx 每次重载（保存后刷新、图片下载完成）都会 BlkFree 整棵块树再
   重建，其中 Mermaid_Parse / Math_Build 是最贵的两步。这里以"源码文本"为键，
   释放时把解析结果收回缓存、重解析时取回——内容未变就零重算。测量不在缓存
   范围，字号/DPI/缩放变化仍会重新测量。容量 FIFO 淘汰，内存有界。 */
typedef struct { unsigned hash; int len; char* key; void* obj; } MCacheEnt;
#define MCACHE_MAX 96
typedef struct {
    MCacheEnt e[MCACHE_MAX];
    int n;
    void (*freeObj)(void*);
} MCache;
static void FreeDiagObj(void* o) { Mermaid_Free((MermaidDiagram*)o); }
static void FreeMathObj(void* o) { Math_Free((MathBox*)o); }
static MCache s_diagC = { { { 0 } }, 0, FreeDiagObj };
static MCache s_mathC = { { { 0 } }, 0, FreeMathObj };

static unsigned McHash(const void* p, int n) {
    unsigned h = 2166136261u;
    const unsigned char* b = (const unsigned char*)p;
    for (int i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static void* McTake(MCache* c, const void* key, int len) {
    if (!key || len <= 0) return NULL;
    unsigned h = McHash(key, len);
    for (int i = 0; i < c->n; i++) {
        if (c->e[i].hash == h && c->e[i].len == len &&
            memcmp(c->e[i].key, key, (size_t)len) == 0) {
            void* o = c->e[i].obj;
            free(c->e[i].key);
            c->e[i] = c->e[c->n - 1];
            c->n--;
            return o;
        }
    }
    return NULL;
}

static void McPut(MCache* c, const void* key, int len, void* obj) {
    if (!obj) return;
    if (!key || len <= 0) { c->freeObj(obj); return; }
    if (c->n >= MCACHE_MAX) {
        free(c->e[0].key);
        c->freeObj(c->e[0].obj);
        for (int i = 1; i < c->n; i++) c->e[i - 1] = c->e[i];
        c->n--;
    }
    char* k = (char*)malloc((size_t)len);
    if (!k) { c->freeObj(obj); return; }
    memcpy(k, key, (size_t)len);
    c->e[c->n].hash = McHash(key, len);
    c->e[c->n].len = len;
    c->e[c->n].key = k;
    c->e[c->n].obj = obj;
    c->n++;
}

static MathBox* MathAcquire(const wchar_t* text) {
    if (!text) return NULL;
    MathBox* m = (MathBox*)McTake(&s_mathC, text, (int)(wcslen(text) * sizeof(wchar_t)));
    if (!m) m = Math_Build(text);
    return m;
}

static void BlkFree(MdBlock* b) {
    if (!b) return;
    for (int i = 0; i < b->nRuns; i++) {
        free(b->runs[i].text);
        free(b->runs[i].href);
        free(b->runs[i].link);
        if (b->runs[i].math) {
            /* 解析结果回收进缓存，重载时按源码取回（见 MCache 注释） */
            McPut(&s_mathC, b->runs[i].text,
                  (int)(b->runs[i].text ? wcslen(b->runs[i].text) * sizeof(wchar_t) : 0),
                  b->runs[i].math);
        }
    }
    free(b->runs);
    free(b->code);
    free(b->hl);
    if (b->diag)
        McPut(&s_diagC, b->mmdSrc, b->mmdSrcLen, b->diag);
    free(b->mmdSrc);
    if (b->cells) {
        for (int i = 0; i < b->nRows * b->nCols; i++) BlkFree(b->cells[i]);
        free(b->cells);
    }
    free(b->aligns);
    for (int i = 0; i < b->nChildren; i++) BlkFree(b->children[i]);
    free(b->children);
    free(b);
}

static wchar_t* Utf8ToW(const char* s, int len) {
    if (len <= 0) len = (int)strlen(s);
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    wchar_t* w = (wchar_t*)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
    w[n] = L'\0';
    return w;
}

typedef struct {
    int tag;            /* TAG_A / TAG_B / ... */
    unsigned bits;
    wchar_t* href;      /* 仅 TAG_A */
} HEnt;

typedef struct {
    MdBlock* stack[64]; int depth;
    MdBlock* textBlk;
    unsigned style;
    HEnt hstk[8]; int hDepth;   /* <a>/<b>/<i>... 与 markdown 链接共用的样式栈 */
    wchar_t* imgSrc;            /* 当前 MD_SPAN_IMG 或 <img> 的图片源 */
    char* codeBuf; int codeLen, codeCap;
    char* mathBuf; int mathLen;
    int inMath; BOOL mathDisp;
    MdBlock* codeBlk;
    int quoteDepth;
    int olStart[64]; char olDelim[64]; int olNum[64]; BOOL inOl[64];
    MdBlock* table;
    int cellCount;
} MdParseCtx;

static MdParseCtx* P;

static const wchar_t* CurLinkHref(void);

static void AddRun(const wchar_t* s, int len, unsigned style) {
    if (!P->textBlk) {
        MdBlock* top = P->stack[P->depth];
        if (top && top->type == MDB_LI) {
            P->textBlk = BlkNew(MDB_P);
            BlkAppend(top, P->textBlk);
        } else {
            return;
        }
    }
    MD_GROW(P->textBlk->runs, P->textBlk->nRuns, P->textBlk->capRuns, MdRun);
    if (!P->textBlk->runs) return;
    MdRun* r = &P->textBlk->runs[P->textBlk->nRuns++];
    r->style = style;
    r->math = NULL;
    r->text = (wchar_t*)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (r->text) {
        memcpy(r->text, s, (size_t)len * sizeof(wchar_t));
        r->text[len] = L'\0';
    }
    r->link = NULL;
    if (style & STY_IMG) {
        r->href = P->imgSrc ? _wcsdup(P->imgSrc) : NULL;
        const wchar_t* lh = CurLinkHref();
        r->link = lh ? _wcsdup(lh) : NULL;
    } else if (style & STY_LINK) {
        const wchar_t* lh = CurLinkHref();
        r->href = lh ? _wcsdup(lh) : NULL;
    } else {
        r->href = NULL;
    }
    r->imgW = 0;
}

static void CodeAppend(const char* s, int len) {
    if (P->codeLen + len + 1 > P->codeCap) {
        int nc = P->codeCap ? P->codeCap * 2 : 512;
        while (nc < P->codeLen + len + 1) nc *= 2;
        char* nb = (char*)realloc(P->codeBuf, nc);
        if (!nb) return;
        P->codeBuf = nb; P->codeCap = nc;
    }
    memcpy(P->codeBuf + P->codeLen, s, (size_t)len);
    P->codeLen += len;
    P->codeBuf[P->codeLen] = 0;
}

static void AttrToWide(MD_ATTRIBUTE a, wchar_t* out, int cch) {
    int n = MultiByteToWideChar(CP_UTF8, 0, a.text, (int)a.size, out, cch - 1);
    if (n < 0) n = 0;
    out[n] = L'\0';
}

/* ===================== 行内 HTML（<a>/<img>/<br>/样式标签）=====================
 * md4c 把原始 HTML 以 MD_TEXT_HTML 交给渲染方，这里把标签映射到既有 run 体系：
 * <a href> → STY_LINK、<img src alt width> → STY_IMG 运行、<br> → STY_BR、
 * b/strong/i/em/del/s/code → 对应样式位，其余标签剥掉、保留内部文本。
 * markdown 原生 [![](img)](link) 与 HTML 标签共用同一套链接样式栈。
 */

enum { TAG_A = 1, TAG_B, TAG_I, TAG_DEL, TAG_CODE, TAGS_IMG = 101, TAGS_BR = 102 };

static void HtmlPushTag(int id, unsigned bits, const wchar_t* href) {
    if (P->hDepth >= 8) return;
    P->hstk[P->hDepth].tag = id;
    P->hstk[P->hDepth].bits = bits;
    P->hstk[P->hDepth].href = href ? _wcsdup(href) : NULL;
    P->hDepth++;
    P->style |= bits;
}

static void HtmlPopTag(int id) {
    for (int k = P->hDepth - 1; k >= 0; k--) {
        if (P->hstk[k].tag == id) {
            for (int j = P->hDepth - 1; j >= k; j--) {
                P->style &= ~P->hstk[j].bits;
                free(P->hstk[j].href);
                P->hstk[j].href = NULL;
            }
            P->hDepth = k;
            return;
        }
    }
}

static const wchar_t* CurLinkHref(void) {
    for (int k = P->hDepth - 1; k >= 0; k--)
        if (P->hstk[k].tag == TAG_A) return P->hstk[k].href;
    return NULL;
}

/* 段落结束时复位，防止未闭合标签跨段泄漏状态 */
static void ResetInlineHtml(void) {
    while (P->hDepth > 0) {
        P->hDepth--;
        P->style &= ~P->hstk[P->hDepth].bits;
        free(P->hstk[P->hDepth].href);
        P->hstk[P->hDepth].href = NULL;
    }
    free(P->imgSrc);
    P->imgSrc = NULL;
}

/* 解码一个 &entity;（常用命名 + 十进制/十六进制数字），返回消耗字节数 */
static int DecodeOneEntity(const char* s, int rem, char* out, int* outLen) {
    *outLen = 0;
    if (rem < 3 || s[0] != '&') return 0;
    static const struct { const char* name; char ch; } kNamed[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' },
        { "quot;", '"' }, { "apos;", '\'' }, { "nbsp;", ' ' },
    };
    if (s[1] != '#') {
        for (int i = 0; i < 6; i++) {
            int n = (int)strlen(kNamed[i].name);
            if (rem >= 1 + n && strncmp(s + 1, kNamed[i].name, n) == 0) {
                out[0] = kNamed[i].ch;
                *outLen = 1;
                return 1 + n;   /* '&' + 名字；多吞一字节会吃掉紧随的内容 */
            }
        }
        return 0;
    }
    unsigned v = 0; int i = 2, digits = 0, hex = 0;
    if (i < rem && (s[i] == 'x' || s[i] == 'X')) { hex = 1; i++; }
    while (i < rem && digits < 7) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * (hex ? 16u : 10u) + (unsigned)d;
        i++; digits++;
    }
    if (digits == 0 || i >= rem || s[i] != ';') return 0;
    if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
    int n;
    if (v < 0x80) { out[0] = (char)v; n = 1; }
    else if (v < 0x800) {
        out[0] = (char)(0xC0 | (v >> 6));
        out[1] = (char)(0x80 | (v & 0x3F));
        n = 2;
    } else if (v < 0x10000) {
        out[0] = (char)(0xE0 | (v >> 12));
        out[1] = (char)(0x80 | ((v >> 6) & 0x3F));
        out[2] = (char)(0x80 | (v & 0x3F));
        n = 3;
    } else {
        out[0] = (char)(0xF0 | (v >> 18));
        out[1] = (char)(0x80 | ((v >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((v >> 6) & 0x3F));
        out[3] = (char)(0x80 | (v & 0x3F));
        n = 4;
    }
    *outLen = n;
    return i + 1;
}

static int DecodeEntitiesUtf8(const char* in, int len, char* out, int cap) {
    int o = 0;
    for (int i = 0; i < len; ) {
        if (in[i] == '&') {
            char tmp[8]; int tl = 0;
            int used = DecodeOneEntity(in + i, len - i, tmp, &tl);
            if (used > 0 && o + tl <= cap) {
                memcpy(out + o, tmp, (size_t)tl);
                o += tl;
                i += used;
                continue;
            }
        }
        out[o++] = in[i++];
    }
    return o;
}

/* 文本节点 → run；blockMode 时按 HTML 规则折叠空白（连续空白 → 单空格） */
static void HtmlAddText(const char* s, int len, BOOL blockMode) {
    if (len <= 0) return;
    char* dbuf = (char*)malloc((size_t)len);
    if (!dbuf) return;
    int dn = DecodeEntitiesUtf8(s, len, dbuf, len);
    wchar_t* w = Utf8ToW(dbuf, dn);
    free(dbuf);
    if (!w) return;
    if (blockMode) {
        wchar_t* w2 = (wchar_t*)malloc(((size_t)wcslen(w) + 1) * sizeof(wchar_t));
        if (w2) {
            int n = 0;
            BOOL sp = FALSE;
            BOOL emitted = P->textBlk && P->textBlk->nRuns > 0;
            for (const wchar_t* p = w; *p; p++) {
                if (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') {
                    sp = TRUE;
                    continue;
                }
                if (sp) {
                    if (n > 0 || emitted) w2[n++] = L' ';
                    sp = FALSE;
                }
                w2[n++] = *p;
            }
            w2[n] = L'\0';
            if (n > 0) AddRun(w2, n, P->style);
            free(w2);
        }
    } else {
        AddRun(w, (int)wcslen(w), P->style);
    }
    free(w);
}

typedef struct {
    int consumed;       /* 整个标签（含 <>）字节数；0 = 不是标签 */
    int tagId;
    BOOL closing;
    char href[2048]; int hrefLen;
    char src[2048];  int srcLen;
    char alt[512];   int altLen;
    int width;
} HtmlTag;

static char TagCharLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int TagNameToId(const char* name) {
    if (!strcmp(name, "a")) return TAG_A;
    if (!strcmp(name, "b") || !strcmp(name, "strong")) return TAG_B;
    if (!strcmp(name, "i") || !strcmp(name, "em")) return TAG_I;
    if (!strcmp(name, "del") || !strcmp(name, "s")) return TAG_DEL;
    if (!strcmp(name, "code")) return TAG_CODE;
    if (!strcmp(name, "img")) return TAGS_IMG;
    if (!strcmp(name, "br")) return TAGS_BR;
    return 0;
}

static BOOL TagIsNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
}

/* 在标签属性区（不含 <name 与 >）解析需要的属性 */
static void ParseAttrs(const char* s, int len, HtmlTag* t) {
    int i = 0;
    while (i < len) {
        while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '/'))
            i++;
        if (i >= len) break;
        char name[16]; int nn = 0;
        while (i < len && TagIsNameChar(s[i]) && nn < 15)
            name[nn++] = TagCharLower(s[i++]);
        name[nn] = '\0';
        if (nn == 0) { i++; continue; }   /* 跳过属性区里的非法字符 */
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        const char* val = NULL; int valLen = 0;
        if (i < len && s[i] == '=') {
            i++;
            while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
            if (i < len && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                val = s + i;
                while (i < len && s[i] != q) i++;
                valLen = i - (int)(val - s);
                if (i < len) i++;
            } else {
                val = s + i;
                while (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '/' && s[i] != '>')
                    i++;
                valLen = i - (int)(val - s);
            }
        }
        if (nn == 0 || valLen <= 0) continue;
        char* dst = NULL; int dstCap = 0, *dstLen = NULL;
        if (!strcmp(name, "href") && t->tagId == TAG_A) { dst = t->href; dstCap = 2048; dstLen = &t->hrefLen; }
        else if (!strcmp(name, "src") && t->tagId == TAGS_IMG) { dst = t->src; dstCap = 2048; dstLen = &t->srcLen; }
        else if (!strcmp(name, "alt") && t->tagId == TAGS_IMG) { dst = t->alt; dstCap = 512; dstLen = &t->altLen; }
        else if (!strcmp(name, "width") && t->tagId == TAGS_IMG) {
            int v = 0, k = 0;
            while (k < valLen && val[k] >= '0' && val[k] <= '9') { v = v * 10 + (val[k] - '0'); k++; }
            if (k > 0 && v >= 1 && v <= 4096) t->width = v;
            continue;
        }
        if (dst) {
            char dec[2048];
            int dn = DecodeEntitiesUtf8(val, valLen < 2048 ? valLen : 2048, dec, 2048);
            int cp = dn < dstCap - 1 ? dn : dstCap - 1;
            memcpy(dst, dec, (size_t)cp);
            dst[cp] = '\0';
            *dstLen = cp;
        }
    }
}

/* 尝试解析 s[0] 起始的一个标签；返回 0 表示不是合法标签（按文本处理） */
static int ParseHtmlTag(const char* s, int rem, HtmlTag* t) {
    ZeroMemory(t, sizeof(*t));
    if (rem < 3 || s[0] != '<') return 0;
    if (s[1] == '!') {
        if (rem >= 4 && s[2] == '-' && s[3] == '-') {
            for (int i = 4; i + 2 < rem; i++)
                if (s[i] == '-' && s[i + 1] == '-' && s[i + 2] == '>')
                    return i + 3;
            return rem;    /* 未闭合注释：整段吞掉 */
        }
        for (int i = 2; i < rem; i++)
            if (s[i] == '>') return i + 1;
        return rem;
    }
    int i = 1;
    if (i < rem && s[i] == '/') { t->closing = TRUE; i++; }
    if (i >= rem || !((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) return 0;
    char name[16]; int nn = 0;
    while (i < rem && TagIsNameChar(s[i])) {
        if (nn < 15) name[nn++] = TagCharLower(s[i]);
        i++;
    }
    name[nn] = '\0';
    int j = i;
    while (j < rem && s[j] != '>') j++;
    if (j >= rem) return 0;
    t->tagId = TagNameToId(name);
    if (!t->closing && (t->tagId == TAG_A || t->tagId == TAGS_IMG))
        ParseAttrs(s + i, j - i, t);
    t->consumed = j + 1;
    return t->consumed;
}

static void HtmlTextToRuns(const char* s, int len, BOOL blockMode) {
    int i = 0;
    while (i < len) {
        if (s[i] == '<') {
            HtmlTag t;
            int tl = ParseHtmlTag(s + i, len - i, &t);
            if (tl > 0) {
                switch (t.tagId) {
                    case TAG_A:
                        if (t.closing) HtmlPopTag(TAG_A);
                        else {
                            wchar_t hw[2048];
                            int n = MultiByteToWideChar(CP_UTF8, 0, t.href,
                                                        t.hrefLen, hw, 2047);
                            if (n < 0) n = 0;
                            hw[n] = L'\0';
                            HtmlPushTag(TAG_A, STY_LINK, hw);
                        }
                        break;
                    case TAG_B:
                        if (t.closing) HtmlPopTag(TAG_B);
                        else HtmlPushTag(TAG_B, STY_BOLD, NULL);
                        break;
                    case TAG_I:
                        if (t.closing) HtmlPopTag(TAG_I);
                        else HtmlPushTag(TAG_I, STY_EM, NULL);
                        break;
                    case TAG_DEL:
                        if (t.closing) HtmlPopTag(TAG_DEL);
                        else HtmlPushTag(TAG_DEL, STY_STRIKE, NULL);
                        break;
                    case TAG_CODE:
                        if (t.closing) HtmlPopTag(TAG_CODE);
                        else HtmlPushTag(TAG_CODE, STY_CODE, NULL);
                        break;
                    case TAGS_BR:
                        if (!t.closing) AddRun(L"", 0, STY_BR | P->style);
                        break;
                    case TAGS_IMG:
                        if (!t.closing) {
                            wchar_t srcW[2048], altW[512];
                            int n = MultiByteToWideChar(CP_UTF8, 0, t.src,
                                                        t.srcLen, srcW, 2047);
                            if (n < 0) n = 0;
                            srcW[n] = L'\0';
                            n = MultiByteToWideChar(CP_UTF8, 0, t.alt,
                                                    t.altLen, altW, 511);
                            if (n < 0) n = 0;
                            altW[n] = L'\0';
                            free(P->imgSrc);
                            P->imgSrc = srcW[0] ? _wcsdup(srcW) : NULL;
                            unsigned save = P->style;
                            P->style |= STY_IMG;
                            AddRun(altW, (int)wcslen(altW), P->style);
                            P->style = save;
                            free(P->imgSrc);
                            P->imgSrc = NULL;
                            if (t.width > 0 && P->textBlk && P->textBlk->nRuns > 0)
                                P->textBlk->runs[P->textBlk->nRuns - 1].imgW = t.width;
                        }
                        break;
                    default:
                        break;  /* 其余标签：剥离，仅保留内部文本 */
                }
                i += tl;
                continue;
            }
        }
        /* 无效标签的 '<'（如 "a < b"）并入后续文本，保证 i 前进 */
        int j = i + (s[i] == '<' ? 1 : 0);
        while (j < len && s[j] != '<') j++;
        HtmlAddText(s + i, j - i, blockMode);
        i = j;
    }
}

/* HTML 块里是否含 <img / <a（决定是否值得按行内规则重写） */
static BOOL BufHasImgOrLink(const char* s, int len) {
    for (int i = 0; i + 4 < len; i++) {
        if (s[i] != '<') continue;
        char a = TagCharLower(s[i + 1]), b = TagCharLower(s[i + 2]);
        if (a == 'i' && b == 'm' && TagCharLower(s[i + 3]) == 'g') return TRUE;
        if (a == 'a' && (b == ' ' || b == '>' || b == '\t' || b == '\r' || b == '\n' || b == '/'))
            return TRUE;
    }
    return FALSE;
}


static int MdEnterBlock(MD_BLOCKTYPE type, void* detail, void* ud) {
    (void)ud;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: {
            MdBlock* q = BlkNew(MDB_QUOTE);
            BlkAppend(P->stack[P->depth], q);
            P->stack[++P->depth] = q;
            P->quoteDepth++;
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: {
            /* 文末脚注区：分隔线 + 有序列表（定义按首次引用顺序送达，编号即 id） */
            MdBlock* hr = BlkNew(MDB_HR);
            BlkAppend(P->stack[P->depth], hr);
            MdBlock* l = BlkNew(MDB_OL);
            BlkAppend(P->stack[P->depth], l);
            P->stack[++P->depth] = l;
            P->inOl[P->depth] = TRUE;
            P->olStart[P->depth] = 1;
            P->olDelim[P->depth] = L'.';
            P->olNum[P->depth] = 0;
            break;
        }
        case MD_BLOCK_FOOTNOTE_DEF: {
            const MD_BLOCK_FOOTNOTE_DEF_DETAIL* d =
                (const MD_BLOCK_FOOTNOTE_DEF_DETAIL*)detail;
            MdBlock* li = BlkNew(MDB_LI);
            li->itemNum = (int)d->id;
            li->itemDelim = L'.';
            li->tight = TRUE;
            BlkAppend(P->stack[P->depth], li);
            P->stack[++P->depth] = li;
            break;
        }
        case MD_BLOCK_UL: {
            MdBlock* l = BlkNew(MDB_UL);
            const MD_BLOCK_UL_DETAIL* d = (const MD_BLOCK_UL_DETAIL*)detail;
            l->tight = d->is_tight;
            BlkAppend(P->stack[P->depth], l);
            P->stack[++P->depth] = l;
            P->inOl[P->depth] = FALSE;
            break;
        }
        case MD_BLOCK_OL: {
            MdBlock* l = BlkNew(MDB_OL);
            const MD_BLOCK_OL_DETAIL* d = (const MD_BLOCK_OL_DETAIL*)detail;
            l->tight = d->is_tight;
            BlkAppend(P->stack[P->depth], l);
            P->stack[++P->depth] = l;
            P->inOl[P->depth] = TRUE;
            P->olStart[P->depth] = (int)d->start;
            P->olDelim[P->depth] = (wchar_t)d->mark_delimiter;
            P->olNum[P->depth] = 0;
            break;
        }
        case MD_BLOCK_LI: {
            MdBlock* li = BlkNew(MDB_LI);
            const MD_BLOCK_LI_DETAIL* d = (const MD_BLOCK_LI_DETAIL*)detail;
            li->isTask = d->is_task;
            li->taskMark = (wchar_t)d->task_mark;
            if (P->inOl[P->depth]) {
                P->olNum[P->depth]++;
                li->itemNum = P->olStart[P->depth] + P->olNum[P->depth] - 1;
                li->itemDelim = P->olDelim[P->depth] ? P->olDelim[P->depth] : L'.';
            }
            li->tight = P->stack[P->depth]->tight;
            BlkAppend(P->stack[P->depth], li);
            P->stack[++P->depth] = li;
            break;
        }
        case MD_BLOCK_H: {
            MdBlock* h = BlkNew(MDB_H);
            h->level = (int)((const MD_BLOCK_H_DETAIL*)detail)->level;
            P->textBlk = h;
            break;
        }
        case MD_BLOCK_P:
            P->textBlk = BlkNew(MDB_P);
            break;
        case MD_BLOCK_CODE: {
            MdBlock* c = BlkNew(MDB_CODE);
            const MD_BLOCK_CODE_DETAIL* d = (const MD_BLOCK_CODE_DETAIL*)detail;
            wchar_t langw[32];
            AttrToWide(d->lang, langw, 32);
            WideCharToMultiByte(CP_UTF8, 0, langw, -1, c->fenceLang, 24, NULL, NULL);
            P->codeBlk = c;
            P->codeLen = 0;
            if (P->codeBuf) P->codeBuf[0] = 0;
            break;
        }
        case MD_BLOCK_HTML: {
            MdBlock* c = BlkNew(MDB_HTML);
            P->codeBlk = c;
            P->codeLen = 0;
            if (P->codeBuf) P->codeBuf[0] = 0;
            break;
        }
        case MD_BLOCK_HR: {
            BlkAppend(P->stack[P->depth], BlkNew(MDB_HR));
            break;
        }
        case MD_BLOCK_TABLE: {
            MdBlock* t = BlkNew(MDB_TABLE);
            const MD_BLOCK_TABLE_DETAIL* d = (const MD_BLOCK_TABLE_DETAIL*)detail;
            t->nCols = (int)d->col_count;
            t->aligns = (int*)calloc((size_t)t->nCols, sizeof(int));
            BlkAppend(P->stack[P->depth], t);
            P->stack[++P->depth] = t;
            P->table = t;
            P->cellCount = 0;
            break;
        }
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            if (P->table) P->table->nRows++;
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            P->textBlk = BlkNew(MDB_P);
            if (P->table && P->textBlk) {
                MD_ALIGN al = ((const MD_BLOCK_TD_DETAIL*)detail)->align;
                int a = (al == MD_ALIGN_CENTER) ? 1 : (al == MD_ALIGN_RIGHT) ? 2 : 0;
                int col = P->cellCount % (P->table->nCols > 0 ? P->table->nCols : 1);
                P->table->aligns[col] = a;
            }
            break;
    }
    return 0;
}

static int MdLeaveBlock(MD_BLOCKTYPE type, void* detail, void* ud) {
    (void)ud; (void)detail;
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE:
            if (P->depth > 0) P->depth--;
            if (P->quoteDepth > 0) P->quoteDepth--;
            break;
        case MD_BLOCK_UL: case MD_BLOCK_OL: case MD_BLOCK_LI:
        case MD_BLOCK_TABLE:
        case MD_BLOCK_FOOTNOTE_DEF: case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            P->textBlk = NULL;
            if (P->depth > 0) P->depth--;
            if (type == MD_BLOCK_TABLE) P->table = NULL;
            break;
        case MD_BLOCK_H: case MD_BLOCK_P:
            if (P->textBlk) BlkAppend(P->stack[P->depth], P->textBlk);
            P->textBlk = NULL;
            ResetInlineHtml();
            break;
        case MD_BLOCK_TH: case MD_BLOCK_TD: {
            MdBlock* cell = P->textBlk;
            P->textBlk = NULL;
            ResetInlineHtml();
            if (!cell || !P->table) { BlkFree(cell); break; }
            MdBlock** nc = (MdBlock**)realloc(P->table->cells,
                ((size_t)P->cellCount + 1) * sizeof(MdBlock*));
            if (nc) {
                P->table->cells = nc;
                nc[P->cellCount++] = cell;
            } else BlkFree(cell);
            break;
        }
        case MD_BLOCK_CODE: case MD_BLOCK_HTML: {
            MdBlock* c = P->codeBlk;
            P->codeBlk = NULL;
            if (!c) break;
            if (P->codeLen > 0) {
                while (P->codeLen > 0 && (P->codeBuf[P->codeLen-1] == '\n' ||
                       P->codeBuf[P->codeLen-1] == '\r')) P->codeLen--;
            }
            if (type == MD_BLOCK_HTML && BufHasImgOrLink(P->codeBuf, P->codeLen)) {
                /* 徽章常见形态：<p align=center>/<div> 包裹 <a><img/></a>。
                 * 按行内规则重写成段落，其余标签剥离、保留文本 */
                MdBlock* p = BlkNew(MDB_P);
                MdBlock* saved = P->textBlk;
                P->textBlk = p;
                HtmlTextToRuns(P->codeBuf, P->codeLen, TRUE);
                ResetInlineHtml();
                P->textBlk = saved;
                BlkAppend(P->stack[P->depth], p);
                BlkFree(c);
                break;
            }
            if (type == MD_BLOCK_CODE && P->codeLen > 0 &&
                _stricmp(c->fenceLang, "mermaid") == 0) {
                c->diag = (MermaidDiagram*)McTake(&s_diagC, P->codeBuf, P->codeLen);
                if (!c->diag) {
                    c->diag = Mermaid_Parse(P->codeBuf, P->codeLen);
                }
                if (c->diag) {
                    /* 留存源码作缓存键：BlkFree 时据此收回缓存 */
                    c->mmdSrc = (char*)malloc((size_t)P->codeLen);
                    if (c->mmdSrc) {
                        memcpy(c->mmdSrc, P->codeBuf, (size_t)P->codeLen);
                        c->mmdSrcLen = P->codeLen;
                    }
                    BlkAppend(P->stack[P->depth], c);
                    break;
                }
                wchar_t hint[256];
                WFmt(hint, L"[%s]", T(STR_MD_MERMAID_UNSUP));
                wchar_t* body = Utf8ToW(P->codeBuf, P->codeLen);
                size_t hl = wcslen(hint);
                c->code = (wchar_t*)malloc((hl + 2 + (body ? wcslen(body) : 0) + 1) * sizeof(wchar_t));
                if (c->code) {
                    wcscpy(c->code, hint);
                    c->code[hl] = L'\n';
                    if (body) wcscpy(c->code + hl + 1, body);
                    c->codeLen = (int)wcslen(c->code);
                }
                free(body);
            } else {
                c->code = Utf8ToW(P->codeBuf, P->codeLen);
                c->codeLen = (int)wcslen(c->code ? c->code : L"");
                /* 代码块语法着色：按围栏语言切片段（未知语言 0 片段） */
                c->hlN = MdHl_Tokenize(c->code, c->codeLen, c->fenceLang, &c->hl);
            }
            BlkAppend(P->stack[P->depth], c);
            break;
        }
        default: break;
    }
    return 0;
}

static int MdEnterSpan(MD_SPANTYPE type, void* detail, void* ud) {
    (void)ud;
    switch (type) {
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            P->inMath = 1;
            P->mathDisp = (type == MD_SPAN_LATEXMATH_DISPLAY);
            P->mathLen = 0;
            P->style |= STY_MATH;
            if (P->mathDisp) P->style |= STY_MATHDISP;
            break;
        case MD_SPAN_STRONG: P->style |= STY_BOLD; break;
        case MD_SPAN_EM:     P->style |= STY_EM; break;
        case MD_SPAN_DEL:    P->style |= STY_STRIKE; break;
        case MD_SPAN_CODE:   P->style |= STY_CODE; break;
        case MD_SPAN_A: {
            const MD_SPAN_A_DETAIL* d = (const MD_SPAN_A_DETAIL*)detail;
            wchar_t tmp[2048];
            AttrToWide(d->href, tmp, 2048);
            HtmlPushTag(TAG_A, STY_LINK, tmp);
            break;
        }
        case MD_SPAN_IMG: {
            P->style |= STY_IMG;
            const MD_SPAN_IMG_DETAIL* d = (const MD_SPAN_IMG_DETAIL*)detail;
            wchar_t tmp[2048];
            AttrToWide(d->src, tmp, 2048);
            free(P->imgSrc);
            P->imgSrc = _wcsdup(tmp);
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            /* 自含 span（内部不触发文本回调）：直接产出 "[N]" 引用文本 */
            const MD_SPAN_FOOTNOTE_REF_DETAIL* d =
                (const MD_SPAN_FOOTNOTE_REF_DETAIL*)detail;
            wchar_t t[16];
            int n = _snwprintf(t, 16, L"[%u]", d->id);
            if (n < 0 || n > 15) n = 15;
            AddRun(t, n, STY_LINK | P->style);
            break;
        }
        default: break;
    }
    return 0;
}

static int MdLeaveSpan(MD_SPANTYPE type, void* detail, void* ud) {
    (void)ud; (void)detail;
    switch (type) {
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY: {
            P->inMath = 0;
            P->style &= ~(STY_MATH | STY_MATHDISP);
            if (P->mathLen > 0 && P->mathBuf) {
                wchar_t* w = Utf8ToW(P->mathBuf, P->mathLen);
                if (w) {
                    AddRun(w, (int)wcslen(w),
                           STY_MATH | (P->mathDisp ? STY_MATHDISP : 0));
                    free(w);
                }
            }
            break;
        }
        case MD_SPAN_STRONG: P->style &= ~STY_BOLD; break;
        case MD_SPAN_EM:     P->style &= ~STY_EM; break;
        case MD_SPAN_DEL:    P->style &= ~STY_STRIKE; break;
        case MD_SPAN_CODE:   P->style &= ~STY_CODE; break;
        case MD_SPAN_A:
            HtmlPopTag(TAG_A);
            break;
        case MD_SPAN_IMG:
            P->style &= ~STY_IMG;
            free(P->imgSrc);
            P->imgSrc = NULL;
            break;
        default: break;
    }
    return 0;
}

static BOOL IsCJK(wchar_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x3000 && c <= 0x303F);
}

/* ---- 单字宽度缓存 ----
   WrapRuns 对 CJK 逐字成 token，逐字 GDI 测量是大文档布局的最大热点
   （几十万次 GetTextExtentPoint32W）。按 HFONT 缓存 65536 项宽度表
   （存 字宽+1，0 表示未缓存）；FontsFree 重建字体时整表失效。 */
#define CW_MAXFONTS 16
static struct { HFONT f; short* w; } s_cw[CW_MAXFONTS];
static int s_nCw = 0;

/* 空格宽度 / TEXTMETRIC 按字体缓存（与字宽表同生命周期） */
static HFONT s_swF[8];
static int s_swV[8];
static HFONT s_tmF[16];
static TEXTMETRICW s_tmV[16];
static BOOL s_tmOk[16];

static void CwInvalidate(void) {
    for (int i = 0; i < s_nCw; i++) free(s_cw[i].w);
    s_nCw = 0;
    memset(s_swF, 0, sizeof(s_swF));   /* 空格宽/字体度量缓存一并失效 */
    memset(s_tmOk, 0, sizeof(s_tmOk));
}

static short* CwTableFor(HFONT f) {
    for (int i = 0; i < s_nCw; i++)
        if (s_cw[i].f == f) return s_cw[i].w;
    if (s_nCw >= CW_MAXFONTS) return NULL;
    short* w = (short*)malloc(sizeof(short) * 65536);
    if (!w) return NULL;
    memset(w, 0, sizeof(short) * 65536);
    s_cw[s_nCw].f = f;
    s_cw[s_nCw].w = w;
    s_nCw++;
    return w;
}

static int SpaceWFor(HDC hdc, HFONT f) {
    for (int i = 0; i < 8; i++)
        if (s_swF[i] == f) return s_swV[i];
    HFONT of = (HFONT)SelectObject(hdc, f);
    SIZE sz;
    GetTextExtentPoint32W(hdc, L" ", 1, &sz);
    SelectObject(hdc, of);
    for (int i = 0; i < 8; i++) {
        if (!s_swF[i]) { s_swF[i] = f; s_swV[i] = sz.cx; break; }
    }
    return sz.cx;
}

static BOOL TmFor(HDC hdc, HFONT f, TEXTMETRICW* tm) {
    for (int i = 0; i < 16; i++) {
        if (s_tmOk[i] && s_tmF[i] == f) {
            *tm = s_tmV[i];
            return TRUE;
        }
    }
    for (int i = 0; i < 16; i++) {
        if (!s_tmOk[i]) {
            GetTextMetricsW(hdc, tm);
            s_tmF[i] = f;
            s_tmV[i] = *tm;
            s_tmOk[i] = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

static int MdText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE len, void* ud) {
    (void)ud;
    switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY: {
            wchar_t* w = Utf8ToW(text, (int)len);
            if (w) { AddRun(w, (int)wcslen(w), P->style); free(w); }
            break;
        }
        case MD_TEXT_CODE:
            if (P->codeBlk) CodeAppend(text, (int)len);
            else {
                wchar_t* w = Utf8ToW(text, (int)len);
                if (w) { AddRun(w, (int)wcslen(w), P->style | STY_CODE); free(w); }
            }
            break;
        case MD_TEXT_HTML: {
            if (P->codeBlk) CodeAppend(text, (int)len);
            else HtmlTextToRuns(text, (int)len, FALSE);
            break;
        }
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            AddRun(L"", 0, STY_BR | P->style);
            break;
        case MD_TEXT_LATEXMATH: {
            int need = P->mathLen + (int)len;
            char* nb = (char*)realloc(P->mathBuf, (size_t)need + 1);
            if (nb) {
                P->mathBuf = nb;
                memcpy(P->mathBuf + P->mathLen, text, (size_t)len);
                P->mathLen = need;
                P->mathBuf[P->mathLen] = 0;
            }
            break;
        }
        case MD_TEXT_NULLCHAR:
        default:
            break;
    }
    return 0;
}

static MD_PARSER g_mdParser = {
    0,
    /* 不用 MD_DIALECT_GITHUB：0.6.0 起它捆绑了 ADMONITIONS（暂未做原生渲染）；
       显式列出所需扩展 + 脚注 */
    MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
    MD_FLAG_TASKLISTS | MD_FLAG_LATEXMATHSPANS | MD_FLAG_FOOTNOTES,
    MdEnterBlock, MdLeaveBlock,
    MdEnterSpan, MdLeaveSpan,
    MdText,
    NULL
};

static MdBlock* MdParseDoc(const char* utf8, DWORD len) {
    MdBlock* root = BlkNew(MDB_QUOTE);
    root->type = MDB_P;
    MdParseCtx ctx;
    ZeroMemory(&ctx, sizeof(ctx));
    ctx.stack[0] = root;
    P = &ctx;
    MdNormalizeMathDelims((char*)utf8, &len);
    md_parse(utf8, (MD_SIZE)len, &g_mdParser, &ctx);
    ResetInlineHtml();
    free(ctx.mathBuf);
    free(ctx.codeBuf);
    return root;
}

static HFONT MkFont(int pt, int weight, BOOL italic, const wchar_t* face) {
    int h = -MulDiv(pt, (int)g_dpi, 72);
    if (h > -4) h = -4;
    return CreateFontW(h, 0, 0, 0, weight, italic, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void FontsFree(void) {
    Mermaid_FontsChanged();
    CwInvalidate();          /* HFONT 可能被新字体复用，字宽表必须整表失效 */
    Math_MeasureCacheReset();
    if (!V.fontsOk) return;
    DeleteObject(V.fonts.body); DeleteObject(V.fonts.bold);
    DeleteObject(V.fonts.emph); DeleteObject(V.fonts.boldemph);
    for (int i = 0; i < 6; i++) DeleteObject(V.fonts.h[i]);
    DeleteObject(V.fonts.mono); DeleteObject(V.fonts.sm);
    DeleteObject(V.fonts.mathIt); DeleteObject(V.fonts.mathUp);
    DeleteObject(V.fonts.mathItS); DeleteObject(V.fonts.mathUpS);
    DeleteObject(V.fonts.mathItSS); DeleteObject(V.fonts.mathUpSS);
    V.fontsOk = FALSE;
}

static void FontsEnsure(HDC hdc) {
    if (V.fontsOk) return;
    int b = g_fontSize;
    MdFonts* f = &V.fonts;
    f->body     = MkFont(b, FW_NORMAL, FALSE, L"Segoe UI");
    f->bold     = MkFont(b, FW_BOLD, FALSE, L"Segoe UI");
    f->emph     = MkFont(b, FW_NORMAL, TRUE, L"Segoe UI");
    f->boldemph = MkFont(b, FW_BOLD, TRUE, L"Segoe UI");
    static const double kHead[6] = { 1.9, 1.55, 1.32, 1.15, 1.0, 1.0 };
    for (int i = 0; i < 6; i++)
        f->h[i] = MkFont((int)(b * kHead[i] + 0.5), FW_BOLD, FALSE, L"Segoe UI");
    f->mono  = MkFont(b, FW_NORMAL, FALSE, L"Consolas");
    f->sm = MkFont(b - 2 > 8 ? b - 2 : 8, FW_NORMAL, FALSE, L"Segoe UI");
    f->mathIt   = MkFont(b,        FW_NORMAL, TRUE,  L"Cambria Math");
    f->mathUp   = MkFont(b,        FW_NORMAL, FALSE, L"Cambria Math");
    f->mathItS  = MkFont((int)(b * 0.72 + 0.5), FW_NORMAL, TRUE,  L"Cambria Math");
    f->mathUpS  = MkFont((int)(b * 0.72 + 0.5), FW_NORMAL, FALSE, L"Cambria Math");
    f->mathItSS = MkFont((int)(b * 0.55 + 0.5), FW_NORMAL, TRUE,  L"Cambria Math");
    f->mathUpSS = MkFont((int)(b * 0.55 + 0.5), FW_NORMAL, FALSE, L"Cambria Math");
    HFONT of = (HFONT)SelectObject(hdc, f->body);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, of);
    f->lineH = tm.tmHeight * 14 / 10;
    V.fontsOk = TRUE;
}

enum { ROLE_BODY = 0, ROLE_H1 = 1, ROLE_MONO = 7, ROLE_SMALL = 8 };

static HFONT FontFor(unsigned style, int role) {
    MdFonts* f = &V.fonts;
    if (role == ROLE_MONO) return f->mono;
    if (role == ROLE_SMALL) return f->sm;
    if (role >= ROLE_H1 && role <= ROLE_H1 + 5) return f->h[role - ROLE_H1];
    if (style & STY_CODE) return f->mono;
    if (style & STY_IMG) return f->emph;
    BOOL b = (style & STY_BOLD) != 0, e = (style & STY_EM) != 0;
    if (b && e) return f->boldemph;
    if (b) return f->bold;
    if (e) return f->emph;
    return f->body;
}

static void ItemResetAll(void) {
    SelReset();
    for (int i = 0; i < V.nItems; i++) {
        if (V.items[i].type == ITM_LINES) {
            for (int l = 0; l < V.items[i].nLines; l++)
                free(V.items[i].lines[l].frags);
            free(V.items[i].lines);
        }
        if (V.items[i].type == ITM_IMAGE && V.items[i].thumb && s_gdipOk)
            p_GdipDisposeImage((GpImage*)V.items[i].thumb);
        if (V.items[i].ownRun) {
            free(V.items[i].ownRun->text);
            free(V.items[i].ownRun->href);
            free(V.items[i].ownRun->link);
            free(V.items[i].ownRun);
        }
        free(V.items[i].href);
    }
    free(V.items);
    V.items = NULL; V.nItems = 0; V.capItems = 0;
}

static MdItem* ItemAlloc(void) {
    MD_GROW(V.items, V.nItems, V.capItems, MdItem);
    if (!V.items) return NULL;
    MdItem* it = &V.items[V.nItems++];
    ZeroMemory(it, sizeof(*it));
    it->role = ROLE_BODY;
    return it;
}

static void ItemPush(int type, RECT rc, int flag) {
    MdItem* it = ItemAlloc();
    if (!it) return;
    it->type = type; it->rc = rc; it->flag = flag;
}

typedef struct Tok { MdRun* run; const wchar_t* s; int len; BOOL space; } Tok;

static int WrapRuns(HDC hdc, MdRun* runs, int nRuns, int avail, int role,
                    MdLine** outLines, int* outN) {
    Tok* toks = NULL; int nT = 0, capT = 0;
    for (int i = 0; i < nRuns; i++) {
        MdRun* r = &runs[i];
        if (r->style & STY_BR) {
            MD_GROW(toks, nT, capT, Tok);
            toks[nT].run = r; toks[nT].s = NULL; toks[nT].len = 0; toks[nT].space = FALSE;
            nT++; continue;
        }
        if (r->style & STY_MATH) {
            MD_GROW(toks, nT, capT, Tok);
            toks[nT].run = r; toks[nT].s = r->text; toks[nT].len = 0; toks[nT].space = FALSE;
            nT++; continue;
        }
        const wchar_t* t = r->text;
        if (!t) continue;
        int L = (int)wcslen(t);
        int i2 = 0;
        while (i2 < L) {
            int start = i2;
            if (t[i2] == L' ' || t[i2] == L'\t') {
                while (i2 < L && (t[i2] == L' ' || t[i2] == L'\t')) i2++;
                MD_GROW(toks, nT, capT, Tok);
                toks[nT].run = r; toks[nT].s = t + start; toks[nT].len = i2 - start; toks[nT].space = TRUE;
                nT++;
            } else if (IsCJK(t[i2])) {
                i2++;
                MD_GROW(toks, nT, capT, Tok);
                toks[nT].run = r; toks[nT].s = t + start; toks[nT].len = 1; toks[nT].space = FALSE;
                nT++;
            } else {
                while (i2 < L && t[i2] != L' ' && t[i2] != L'\t' && !IsCJK(t[i2])) i2++;
                MD_GROW(toks, nT, capT, Tok);
                toks[nT].run = r; toks[nT].s = t + start; toks[nT].len = i2 - start; toks[nT].space = FALSE;
                nT++;
            }
        }
    }

    MdLine* lines = NULL; int nL = 0, capL = 0;
    MdFrag* fr = NULL; int nF = 0, capF = 0;
    int x = 0, lineH = 0, asc = 0;
    HFONT curFont = NULL;
    TEXTMETRICW curTm; ZeroMemory(&curTm, sizeof(curTm));

    int spaceW = 0;
    {
        HFONT bf = FontFor(0, role == ROLE_MONO || role == ROLE_SMALL ? role : ROLE_BODY);
        spaceW = SpaceWFor(hdc, bf);
    }

    for (int i = 0; i <= nT; i++) {
        BOOL flush = (i == nT);
        if (!flush && (toks[i].run->style & STY_BR)) flush = TRUE;
        BOOL overflow = FALSE;
        if (!flush) {
            Tok* tk = &toks[i];
            if (tk->space) {
                if (nF > 0) {
                    MD_GROW(fr, nF, capF, MdFrag);
                    fr[nF].run = tk->run; fr[nF].s = tk->s; fr[nF].len = tk->len;
                    fr[nF].x = x; fr[nF].w = 0;
                    nF++;
                    x += spaceW;
                }
                continue;
            }
            if (tk->run->style & STY_MATH) {
                MdRun* mr2 = tk->run;
                if (!mr2->math) mr2->math = MathAcquire(mr2->text);
                if (mr2->math) Math_Measure(mr2->math, hdc, &V.fonts);
                int mw = mr2->math ? Math_Width(mr2->math) : UI_Scale(60);
                int ma = mr2->math ? Math_Ascent(mr2->math) : V.fonts.lineH;
                int mh = mr2->math ? Math_Height(mr2->math) : V.fonts.lineH;
                if (nF > 0 && x + mw > avail) { flush = TRUE; overflow = TRUE; }
                else {
                    if (mh > lineH) lineH = mh;
                    if (ma > asc) asc = ma;
                    MD_GROW(fr, nF, capF, MdFrag);
                    fr[nF].run = mr2; fr[nF].s = mr2->text; fr[nF].len = 0;
                    fr[nF].x = x; fr[nF].w = mw;
                    nF++;
                    x += mw;
                    continue;
                }
            } else {
                HFONT fnt = FontFor(tk->run->style, role);
                if (fnt != curFont) {
                    SelectObject(hdc, fnt);
                    if (!TmFor(hdc, fnt, &curTm)) GetTextMetricsW(hdc, &curTm);
                    curFont = fnt;
                }
                int tw;
                short* cwt = (tk->len == 1) ? CwTableFor(fnt) : NULL;
                if (cwt) {
                    unsigned ch = (unsigned)tk->s[0];
                    if (cwt[ch]) {
                        tw = (int)cwt[ch] - 1;
                    } else {
                        SIZE sz;
                        GetTextExtentPoint32W(hdc, tk->s, 1, &sz);
                        tw = sz.cx;
                        cwt[ch] = (short)(tw + 1);
                    }
                } else {
                    SIZE sz;
                    GetTextExtentPoint32W(hdc, tk->s, tk->len, &sz);
                    tw = sz.cx;
                }
                if (nF > 0 && x + tw > avail) { flush = TRUE; overflow = TRUE; }
                else {
                    if (curTm.tmHeight > lineH) lineH = curTm.tmHeight;
                    if (curTm.tmAscent > asc) asc = curTm.tmAscent;
                    MD_GROW(fr, nF, capF, MdFrag);
                    fr[nF].run = tk->run; fr[nF].s = tk->s; fr[nF].len = tk->len;
                    fr[nF].x = x; fr[nF].w = tw;
                    nF++;
                    x += tw;
                    continue;
                }
            }
        }
        if (nF > 0) {
            while (nF > 0 && fr[nF-1].run->text && fr[nF-1].len > 0 &&
                   (fr[nF-1].s[0] == L' ')) {
                nF--;
            }
            MD_GROW(lines, nL, capL, MdLine);
            MdLine* ln = &lines[nL++];
            ln->frags = fr; ln->nF = nF;
            ln->h = lineH ? lineH : V.fonts.lineH;
            ln->asc = asc;
            ln->y = 0;
            fr = NULL; nF = 0; capF = 0;
        }
        x = 0; lineH = 0; asc = 0;
        if (overflow) i--;
    }
    free(toks);
    if (curFont) SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    *outLines = lines;
    *outN = nL;
    return nL;
}

static int LayoutText(HDC hdc, MdBlock* b, int x0, int avail, int* y, int role,
                      UINT fmtExtra) {
    (void)fmtExtra;
    if (b->nRuns == 0) return 0;
    MdLine* lines; int nL;
    WrapRuns(hdc, b->runs, b->nRuns, avail, role, &lines, &nL);
    if (nL == 0) { free(lines); return 0; }
    int h = 0;
    for (int i = 0; i < nL; i++) {
        lines[i].y = h;
        h += lines[i].h + V.fonts.lineH / 5;
    }
    RECT rc = { x0, *y, x0 + avail, *y + h };
    MdItem* it = ItemAlloc();
    if (!it) { free(lines); return 0; }
    it->type = ITM_LINES; it->rc = rc; it->lines = lines; it->nLines = nL;
    it->role = role;
    *y += h;
    return h;
}

static unsigned StyleOfHl(unsigned char t) {
    switch (t) {
        case HL_KW:  return STY_KW;
        case HL_STR: return STY_STR;
        case HL_COM: return STY_COM;
        case HL_NUM: return STY_NUM;
        case HL_PRE: return STY_PRE;
    }
    return 0;
}

static void LayoutCodeBlock(HDC hdc, MdBlock* b, int x0, int avail, int* y) {
    if (b->diag) {
        SIZE sz = Mermaid_Measure(b->diag, hdc, &V.fonts);
        int w = sz.cx, h = sz.cy;
        RECT bg = { x0, *y, x0 + w + UI_Scale(16), *y + h + UI_Scale(16) };
        ItemPush(ITM_RECT, bg, RCT_CANVAS);
        MdItem* it = ItemAlloc();
        if (!it) return;
        it->type = ITM_DIAGRAM;
        it->rc.left = x0 + UI_Scale(8); it->rc.top = *y + UI_Scale(8);
        it->rc.right = it->rc.left + w; it->rc.bottom = it->rc.top + h;
        it->diag = b->diag;
        *y += h + UI_Scale(16) + UI_Scale(12);
        return;
    }
    /* 1) 逐行把代码切成片段（hl 覆盖段 + 普通补缝段；跨行片段按行边界截断） */
    typedef struct { int off, len; unsigned sty; } Piece;
    Piece* pcs = NULL; int nP = 0, capP = 0;
    int* lineBegin = NULL; int nSrcLines = 0, capLB = 0;
    int total = b->codeLen, pos = 0;
    while (pos < total) {
        int e = pos;
        while (e < total && b->code[e] != L'\n') e++;
        int lineLen = e - pos;
        if (lineLen > 0 && b->code[pos + lineLen - 1] == L'\r') lineLen--;
        MD_GROW(lineBegin, nSrcLines, capLB, int);
        lineBegin[nSrcLines++] = nP;
        int c2 = 0;
        do {
            int end = lineLen; unsigned sty = 0;
            for (int s = 0; s < b->hlN; s++) {
                int a = b->hl[s].pos, z = a + b->hl[s].len;
                if (z <= pos + c2) continue;              /* 片段已消费 */
                if (a <= pos + c2) {                      /* 正处于片段内 */
                    int take = (z < pos + lineLen) ? z : pos + lineLen;
                    end = take - pos;
                    sty = StyleOfHl(b->hl[s].type);
                    break;
                }
                if (a - pos < end) end = a - pos;         /* 前方片段限制普通段 */
            }
            if (end < c2) end = c2;
            if (end > lineLen) end = lineLen;
            MD_GROW(pcs, nP, capP, Piece);
            pcs[nP].off = pos + c2;
            pcs[nP].len = end - c2;
            pcs[nP].sty = sty;
            nP++;
            c2 = end;
        } while (c2 < lineLen);
        pos = (e < total) ? e + 1 : total;
    }

    /* 2) 片段文本复制进一块连续暂存（各自 NUL 结尾）：
          runs[0] 为哑头——ownRun 释放逻辑只 free 首元素 text/href/link
          与整个数组，暂存缓冲即由哑头持有；其余 run 的 text 指向暂存内部 */
    int scratchChars = 1;
    for (int i = 0; i < nP; i++) scratchChars += pcs[i].len + 1;
    wchar_t* scratch = (wchar_t*)malloc((size_t)scratchChars * sizeof(wchar_t));
    MdRun* runs = (MdRun*)calloc((size_t)nP + 1, sizeof(MdRun));
    MdLine* lines = NULL; int nL = 0, capL = 0;
    if (scratch && runs) {
        scratch[0] = 0;
        runs[0].text = scratch;        /* 哑头持有暂存 */
        runs[0].style = 0; runs[0].href = NULL; runs[0].link = NULL;
        int off = 1;
        for (int i = 0; i < nP; i++) {
            int start = off;
            memcpy(scratch + off, b->code + pcs[i].off, (size_t)pcs[i].len * sizeof(wchar_t));
            off += pcs[i].len;
            scratch[off++] = 0;
            runs[i + 1].text = scratch + start;
            runs[i + 1].style = pcs[i].sty;
            runs[i + 1].href = NULL;
            runs[i + 1].link = NULL;
        }
        for (int l = 0; l < nSrcLines; l++) {
            int begin = lineBegin[l] + 1;
            int end = (l + 1 < nSrcLines) ? lineBegin[l + 1] + 1 : nP + 1;
            MdLine* sub; int subN;
            WrapRuns(hdc, runs + begin, end - begin, avail - UI_Scale(20), ROLE_MONO, &sub, &subN);
            MD_GROW(lines, nL + subN, capL, MdLine);
            for (int i = 0; i < subN; i++) lines[nL++] = sub[i];
            free(sub);
        }
    }
    free(pcs);
    free(lineBegin);
    int pad = UI_Scale(10);
    int h = 0;
    for (int i = 0; i < nL; i++) {
        lines[i].y = h;
        h += lines[i].h + UI_Scale(2);
    }
    if (nL == 0) h = pad * 2;
    RECT bg = { x0, *y, x0 + avail, *y + h + pad * 2 };
    ItemPush(ITM_RECT, bg, RCT_CODEBG);
    ItemPush(ITM_FRAME, bg, 0);
    MdItem* it = ItemAlloc();
    if (!it) return;
    it->type = ITM_LINES;
    it->rc.left = x0 + pad; it->rc.top = *y + pad;
    it->rc.right = x0 + avail - pad; it->rc.bottom = *y + pad + h;
    it->lines = lines; it->nLines = nL;
    it->role = ROLE_MONO;
    it->ownRun = runs;
    if (!scratch || !runs) { free(scratch); free(runs); it->ownRun = NULL; }
    *y += h + pad * 2 + UI_Scale(12);
}

static void LayoutTable(HDC hdc, MdBlock* b, int x0, int avail, int* y) {
    int nC = b->nCols, nR = b->nRows;
    if (nC <= 0 || nR <= 0 || !b->cells) return;
    int* colW = (int*)calloc((size_t)nC, sizeof(int));
    int pad = UI_Scale(7);
    HFONT hf0 = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
    HFONT cf = NULL;
    for (int r = 0; r < nR; r++) {
        for (int c = 0; c < nC; c++) {
            MdBlock* cell = b->cells[r * nC + c];
            for (int i = 0; i < cell->nRuns; i++) {
                HFONT f = FontFor(cell->runs[i].style, ROLE_BODY);
                if (f != cf) { SelectObject(hdc, f); cf = f; }
                SIZE sz;
                GetTextExtentPoint32W(hdc, cell->runs[i].text,
                                      (int)wcslen(cell->runs[i].text), &sz);
                int w = sz.cx;
                if (w > colW[c]) colW[c] = w;
            }
        }
    }
    SelectObject(hdc, hf0);
    int total = pad * 2 * nC;
    for (int c = 0; c < nC; c++) total += colW[c];
    if (total > avail) {
        int natTotal = total;
        for (int c = 0; c < nC; c++) {
            colW[c] = colW[c] * (avail - pad * 2 * nC) / (natTotal - pad * 2 * nC);
            if (colW[c] < UI_Scale(30)) colW[c] = UI_Scale(30);
        }
        total = pad * 2 * nC;
        for (int c = 0; c < nC; c++) total += colW[c];
    }
    typedef struct { MdLine* lines; int nL; int h; } CellLay;
    CellLay* lays = (CellLay*)calloc((size_t)nR * nC, sizeof(CellLay));
    for (int r = 0; r < nR; r++) {
        for (int c = 0; c < nC; c++) {
            MdBlock* cell = b->cells[r * nC + c];
            CellLay* cl = &lays[r * nC + c];
            if (cell->nRuns == 0) { cl->h = V.fonts.lineH; continue; }
            WrapRuns(hdc, cell->runs, cell->nRuns, colW[c] - 2, ROLE_BODY, &cl->lines, &cl->nL);
            int hh = 0;
            for (int i = 0; i < cl->nL; i++) {
                cl->lines[i].y = hh;
                hh += cl->lines[i].h + V.fonts.lineH / 5;
            }
            cl->h = hh ? hh : V.fonts.lineH;
        }
    }
    int* rowH = (int*)calloc((size_t)nR, sizeof(int));
    for (int r = 0; r < nR; r++) {
        int mx = V.fonts.lineH;
        for (int c = 0; c < nC; c++) {
            int need = lays[r * nC + c].h + pad * 2;
            if (need > mx) mx = need;
        }
        rowH[r] = mx;
    }
    if (nR > 0) {
        RECT hd = { x0, *y, x0 + total, *y + rowH[0] };
        ItemPush(ITM_RECT, hd, RCT_TABLEHEAD);
    }
    int yy = *y;
    for (int r = 0; r < nR; r++) {
        int xx = x0;
        for (int c = 0; c < nC; c++) {
            CellLay* cl = &lays[r * nC + c];
            if (cl->nL > 0) {
                RECT rc = { xx + pad, yy + pad, xx + colW[c] - pad, yy + rowH[r] - pad };
                MdItem* it = ItemAlloc();
                if (!it) continue;
                it->type = ITM_LINES; it->rc = rc;
                it->lines = cl->lines; it->nLines = cl->nL;
                it->role = ROLE_BODY;
                int align = b->aligns ? b->aligns[c] : 0;
                for (int i = 0; i < cl->nL; i++) {
                    int lw = 0;
                    if (cl->lines[i].nF > 0)
                        lw = cl->lines[i].frags[cl->lines[i].nF - 1].x +
                             cl->lines[i].frags[cl->lines[i].nF - 1].w;
                    int off = (align == 1) ? (colW[c] - pad * 2 - lw) / 2 :
                              (align == 2) ? (colW[c] - pad * 2 - lw) : 0;
                    if (off < 0) off = 0;
                    for (int k = 0; k < cl->lines[i].nF; k++)
                        cl->lines[i].frags[k].x += off;
                }
            }
            if (c + 1 < nC) {
                RECT vl = { xx + colW[c] + pad, yy, xx + colW[c] + pad + 1, yy + rowH[r] };
                ItemPush(ITM_RECT, vl, RCT_CODEBG);
            }
            xx += colW[c] + pad * 2;
        }
        if (r == 0) {
            RECT hl = { x0, yy + rowH[r] - 1, x0 + total, yy + rowH[r] };
            ItemPush(ITM_RECT, hl, RCT_HRULE);
        }
        yy += rowH[r];
    }
    RECT outer = { x0, *y, x0 + total, yy };
    ItemPush(ITM_FRAME, outer, 0);
    *y = yy + UI_Scale(12);
    free(colW); free(lays); free(rowH);
}

static void LayoutBlock(HDC hdc, MdBlock* b, int x0, int avail, int* y, int quoteDepth);

static void LayoutList(HDC hdc, MdBlock* b, int x0, int avail, int* y, int qd) {
    int itemGap = b->tight ? UI_Scale(3) : UI_Scale(8);
    for (int i = 0; i < b->nChildren; i++) {
        MdBlock* li = b->children[i];
        int startY = *y;
        wchar_t mark[32];
        if (li->isTask)
            wcscpy(mark, (li->taskMark == L' ' || !li->taskMark) ? L"☐" : L"☑");
        else if (b->type == MDB_OL)
            WFmt(mark, L"%d%c", li->itemNum, li->itemDelim ? li->itemDelim : L'.');
        else
            wcscpy(mark, L"•");
        int mw = 0;
        {
            HFONT of = (HFONT)SelectObject(hdc, V.fonts.body);
            SIZE sz;
            GetTextExtentPoint32W(hdc, mark, (int)wcslen(mark), &sz);
            mw = sz.cx;
            SelectObject(hdc, of);
        }
        int indent = mw + UI_Scale(10);
        RECT mk = { x0, *y, x0 + mw, *y + V.fonts.lineH };
        MdItem* it = ItemAlloc();
        if (it) {
            it->type = ITM_LINES; it->rc = mk; it->role = ROLE_BODY;
            MdRun* mr = (MdRun*)calloc(1, sizeof(MdRun));
            mr->text = _wcsdup(mark);
            mr->style = 0; mr->href = NULL;
            it->ownRun = mr;
            MdLine* ln = (MdLine*)calloc(1, sizeof(MdLine));
            MdFrag* fr = (MdFrag*)calloc(1, sizeof(MdFrag));
            fr->run = mr; fr->s = mr->text; fr->len = (int)wcslen(mark);
            fr->x = 0; fr->w = mw;
            ln->frags = fr; ln->nF = 1;
            HFONT of = (HFONT)SelectObject(hdc, V.fonts.body);
            TEXTMETRICW tm; GetTextMetricsW(hdc, &tm);
            SelectObject(hdc, of);
            ln->h = tm.tmHeight; ln->asc = tm.tmAscent; ln->y = 0;
            it->lines = ln; it->nLines = 1;
        }
        int inner = *y;
        for (int c = 0; c < li->nChildren; c++)
            LayoutBlock(hdc, li->children[c], x0 + indent, avail - indent, &inner, qd);
        *y = inner > *y ? inner : *y + V.fonts.lineH;
        *y += itemGap;
        (void)startY;
    }
    *y += UI_Scale(2);
}

static BOOL ResolveLocalHref(const wchar_t* href, wchar_t* full, int cch) {
    full[0] = L'\0';
    if (!href || !*href || href[0] == L'#') return FALSE;
    if (_wcsnicmp(href, L"http://", 7) == 0 || _wcsnicmp(href, L"https://", 8) == 0 ||
        _wcsnicmp(href, L"mailto:", 7) == 0) return FALSE;
    if (PathIsRelativeW(href) && V.baseDir[0])
        PathCombineW(full, V.baseDir, href);
    else
        wcscpy_s(full, cch, href);
    return TRUE;
}

static BOOL IsHttpUrl(const wchar_t* s) {
    return s && (_wcsnicmp(s, L"http://", 7) == 0 || _wcsnicmp(s, L"https://", 8) == 0);
}

/* 占位框标签：alt 优先，否则取 URL 末段（%20 还原为空格） */
static void ImgFallbackLabel(const wchar_t* src, const wchar_t* alt, wchar_t* out, int cch) {
    if (alt && *alt) {
        wcsncpy_s(out, cch, alt, _TRUNCATE);
        return;
    }
    const wchar_t* seg = L"image";
    wchar_t buf[64];
    if (src && *src) {
        const wchar_t* slash = wcsrchr(src, L'/');
        const wchar_t* p = slash ? slash + 1 : src;
        int n = 0;
        while (*p && *p != L'?' && *p != L'#' && n < 40) {
            if (p[0] == L'%' && p[1] == L'2' && p[2] == L'0') { buf[n++] = L' '; p += 3; }
            else buf[n++] = *p++;
        }
        buf[n] = L'\0';
        if (n > 0) seg = buf;
    }
    wcsncpy_s(out, cch, seg, _TRUNCATE);
}

/* 纯图片段落：单图或多图徽章行，横排、超宽换行；
 * 加载失败（SVG/远程未就绪/本地缺失）画可点击占位框 */
typedef struct { MdRun* run; int w, h; ImgEnt* ent; int isBr; } ImgU;

static BOOL LayoutImageParagraph(HDC hdc, MdBlock* b, int x0, int avail, int* y) {
    if (b->nRuns <= 0) return FALSE;
    ImgU* U = (ImgU*)calloc((size_t)b->nRuns, sizeof(ImgU));
    if (!U) return FALSE;
    int nU = 0, nImg = 0;
    for (int i = 0; i < b->nRuns; i++) {
        MdRun* r = &b->runs[i];
        if (r->style & STY_BR) {
            if (nU >= b->nRuns) break;
            U[nU].isBr = TRUE;
            nU++;
            continue;
        }
        if (!(r->style & STY_IMG)) {
            BOOL ws = TRUE;
            if (!r->text) ws = FALSE;
            else for (const wchar_t* p = r->text; *p; p++)
                if (*p != L' ' && *p != L'\t') { ws = FALSE; break; }
            if (!ws) { free(U); return FALSE; }
            continue;
        }
        U[nU].run = r;
        nU++;
        nImg++;
    }
    if (nImg == 0) { free(U); return FALSE; }

    HFONT of = (HFONT)SelectObject(hdc, V.fonts.body);
    for (int i = 0; i < nU; i++) {
        if (U[i].isBr) continue;
        MdRun* r = U[i].run;
        const wchar_t* src = r->href ? r->href : L"";
        ImgEnt* e = NULL;
        if (IsHttpUrl(src)) {
            wchar_t full[MAX_PATH];
            if (MdImg_CachePath(src, full, MAX_PATH) &&
                GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES)
                e = ImgGet(full);
            else
                MdImg_Ensure(src);   /* 异步下载，完成后经 WM_MDIMG_READY 重排 */
        } else {
            wchar_t full[MAX_PATH];
            if (ResolveLocalHref(src, full, MAX_PATH)) e = ImgGet(full);
        }
        U[i].ent = e;
        if (e) {
            UINT w = e->w, h = e->h;
            if (r->imgW > 0 && w > 0) {
                int tw = UI_Scale(r->imgW);
                if (tw > avail) tw = avail;
                if (tw > 0) { h = (UINT)((double)h * tw / w); w = (UINT)tw; }
            }
            if (w > (UINT)avail && w > 0) { h = (UINT)((double)h * avail / w); w = (UINT)avail; }
            U[i].w = (int)w;
            U[i].h = (int)h;
        } else {
            wchar_t label[80];
            ImgFallbackLabel(src, r->text, label, 80);
            SIZE sz = { UI_Scale(20), 0 };
            GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &sz);
            int w = sz.cx + UI_Scale(24);
            if (r->imgW > 0) {
                int tw = UI_Scale(r->imgW);
                if (tw > w) w = tw;
            }
            if (w > avail) w = avail;
            if (w < UI_Scale(48)) w = UI_Scale(48);
            U[i].w = w;
            U[i].h = V.fonts.lineH + UI_Scale(12);
        }
        if (U[i].w <= 0 || U[i].h <= 0) { U[i].w = UI_Scale(48); U[i].h = V.fonts.lineH; }
    }

    int cx = x0, rowH = 0;
    for (int i = 0; i < nU; i++) {
        if (U[i].isBr || (cx + U[i].w > x0 + avail && cx > x0)) {
            *y += rowH + UI_Scale(6);
            cx = x0;
            rowH = 0;
            if (U[i].isBr) continue;
        }
        MdRun* r = U[i].run;
        const wchar_t* click = r->link ? r->link
                             : (IsHttpUrl(r->href) ? r->href : NULL);
        if (U[i].ent) {
            MdItem* it = ItemAlloc();
            if (it) {
                it->type = ITM_IMAGE;
                it->rc.left = cx; it->rc.top = *y;
                it->rc.right = cx + U[i].w; it->rc.bottom = *y + U[i].h;
                it->image = U[i].ent->img;
                it->imageW = U[i].ent->w; it->imageH = U[i].ent->h;
                {   /* 源矩形必须按位图真实尺寸：SVG 光栅为逻辑尺寸的 2 倍，
                       沿用逻辑值会只画左上四分之一 */
                    UINT bw = 0, bh = 0;
                    if (p_GdipGetImageWidth(U[i].ent->img, &bw) == 0 && bw &&
                        p_GdipGetImageHeight(U[i].ent->img, &bh) == 0 && bh) {
                        it->imageW = bw; it->imageH = bh;
                    }
                }
                it->href = click ? _wcsdup(click) : NULL;
            }
        } else {
            wchar_t label[80];
            ImgFallbackLabel(r->href ? r->href : L"", r->text, label, 80);
            MdRun* mr = (MdRun*)calloc(1, sizeof(MdRun));
            MdLine* ln = (MdLine*)calloc(1, sizeof(MdLine));
            MdFrag* fr = (MdFrag*)calloc(1, sizeof(MdFrag));
            if (mr && ln && fr) {
                mr->text = _wcsdup(label);
                mr->style = STY_LINK;
                mr->href = click ? _wcsdup(click) : NULL;
                SIZE sz = { 0, 0 };
                if (mr->text)
                    GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &sz);
                TEXTMETRICW tm;
                GetTextMetricsW(hdc, &tm);
                if (mr->text) {
                    fr->run = mr; fr->s = mr->text; fr->len = (int)wcslen(mr->text);
                    fr->x = 0; fr->w = sz.cx;
                    ln->frags = fr; ln->nF = 1;
                    ln->h = tm.tmHeight; ln->asc = tm.tmAscent; ln->y = 0;
                    RECT box = { cx, *y, cx + U[i].w, *y + U[i].h };
                    ItemPush(ITM_FRAME, box, 0);
                    MdItem* it = ItemAlloc();
                    if (it) {
                        it->type = ITM_LINES;
                        int tx = cx + (U[i].w - sz.cx) / 2;
                        if (tx < cx + UI_Scale(6)) tx = cx + UI_Scale(6);
                        int ty = *y + (U[i].h - (int)tm.tmHeight) / 2;
                        if (ty < *y + UI_Scale(4)) ty = *y + UI_Scale(4);
                        it->rc.left = tx; it->rc.top = ty;
                        it->rc.right = cx + U[i].w; it->rc.bottom = *y + U[i].h;
                        it->lines = ln; it->nLines = 1;
                        it->role = ROLE_BODY;
                        it->ownRun = mr;
                    } else {
                        free(ln);
                        free(fr);
                        free(mr->text); free(mr->href); free(mr);
                        mr = NULL;
                    }
                } else {
                    free(ln); free(fr);
                    free(mr->href); free(mr);
                    mr = NULL;
                }
            } else {
                free(ln); free(fr);
                if (mr) { free(mr->text); free(mr->href); free(mr); }
            }
        }
        cx += U[i].w + UI_Scale(6);
        if (U[i].h > rowH) rowH = U[i].h;
    }
    SelectObject(hdc, of);
    *y += rowH + UI_Scale(9);
    free(U);
    return TRUE;
}

static BOOL LayoutMathParagraph(HDC hdc, MdBlock* b, int x0, int avail, int* y) {
    MdRun* mr = NULL;
    for (int i = 0; i < b->nRuns; i++) {
        MdRun* r = &b->runs[i];
        if (r->style & STY_BR) continue;
        if (!(r->style & STY_MATHDISP)) {
            if (r->text)
                for (const wchar_t* p = r->text; *p; p++)
                    if (*p != L' ' && *p != L'\t') return FALSE;
            continue;
        }
        if (mr) return FALSE;
        mr = r;
    }
    if (!mr) return FALSE;
    if (!mr->math) mr->math = MathAcquire(mr->text);
    if (!mr->math) return FALSE;
    Math_Measure(mr->math, hdc, &V.fonts);
    MdItem* it = ItemAlloc();
    if (!it) return FALSE;
    it->type = ITM_MATH;
    int cx = x0 + (avail - Math_Width(mr->math)) / 2;
    if (cx < x0) cx = x0;
    it->rc.left = cx;
    it->rc.top = *y + UI_Scale(6);
    it->rc.right = cx + Math_Width(mr->math);
    it->rc.bottom = it->rc.top + Math_Height(mr->math);
    it->mathRun = mr;
    *y += Math_Height(mr->math) + UI_Scale(6) + UI_Scale(9);
    return TRUE;
}

static void LayoutBlock(HDC hdc, MdBlock* b, int x0, int avail, int* y, int quoteDepth) {
    switch (b->type) {
        case MDB_P:
            if (LayoutMathParagraph(hdc, b, x0, avail, y)) break;
            if (LayoutImageParagraph(hdc, b, x0, avail, y)) break;
            LayoutText(hdc, b, x0, avail, y, ROLE_BODY, 0);
            *y += UI_Scale(9);
            break;
        case MDB_H: {
            int lvl = b->level < 1 ? 1 : (b->level > 6 ? 6 : b->level);
            static const int before[6] = { 22, 18, 15, 13, 12, 12 };
            static const int after[6]  = { 10, 9, 7, 6, 6, 6 };
            *y += UI_Scale(before[lvl - 1]);
            LayoutText(hdc, b, x0, avail, y, ROLE_H1 + lvl - 1, 0);
            if (lvl <= 2) {
                RECT hr = { x0, *y + UI_Scale(4), x0 + avail, *y + UI_Scale(5) };
                ItemPush(ITM_RECT, hr, RCT_HRULE);
                *y += UI_Scale(4) + 1;
            }
            *y += UI_Scale(after[lvl - 1]);
            break;
        }
        case MDB_CODE: case MDB_HTML:
            LayoutCodeBlock(hdc, b, x0, avail, y);
            break;
        case MDB_HR: {
            *y += UI_Scale(10);
            RECT hr = { x0, *y, x0 + avail, *y + 2 };
            ItemPush(ITM_RECT, hr, RCT_HRULE);
            *y += 2 + UI_Scale(12);
            break;
        }
        case MDB_QUOTE: {
            int yStart = *y;
            int indent = UI_Scale(24);
            int inner = *y;
            for (int i = 0; i < b->nChildren; i++)
                LayoutBlock(hdc, b->children[i], x0 + indent, avail - indent, &inner, quoteDepth + 1);
            if (inner > yStart) {
                RECT bar = { x0 + UI_Scale(4), yStart, x0 + UI_Scale(7), inner };
                ItemPush(ITM_RECT, bar, RCT_QUOTEBAR);
            }
            *y = inner + UI_Scale(9);
            break;
        }
        case MDB_UL: case MDB_OL:
            LayoutList(hdc, b, x0, avail, y, quoteDepth);
            break;
        case MDB_LI:
            for (int i = 0; i < b->nChildren; i++)
                LayoutBlock(hdc, b->children[i], x0, avail, y, quoteDepth);
            break;
        case MDB_TABLE:
            LayoutTable(hdc, b, x0, avail, y);
            break;
        default:
            for (int i = 0; i < b->nChildren; i++)
                LayoutBlock(hdc, b->children[i], x0, avail, y, quoteDepth);
            break;
    }
}

/* ---- 增量布局 ----
   长文档首次打开只布局到 可视区+一屏 余量，滚动时按需向下延伸（布局永远
   从 y=0 顺序推进，向上滚必然命中）。未布局部分的总高用"已布局平均块高"
   估算，滚动条随布局推进逐步收敛；END 键与打印强制补全拿到精确值。 */
static int DocHEstimate(void) {
    int base = V.layY + UI_Scale(40);
    if (V.layDone || !V.root) return base;
    int remaining = V.root->nChildren - V.layIdx;
    if (V.layIdx > 0 && remaining > 0) {
        double avg = (double)(V.layY - UI_Scale(16)) / (double)V.layIdx;
        if (avg < 1.0) avg = 1.0;
        return V.layY + (int)(avg * (double)remaining) + UI_Scale(40);
    }
    return base;
}

static void LayoutBegin(void) {
    ItemResetAll();
    V.layIdx = 0;
    V.layY = UI_Scale(16);
    V.layDone = (V.root == NULL || V.root->nChildren == 0);
    V.docH = DocHEstimate();
    V.lastLayoutW = V.clientW;
}

static void LayoutEnsure(int throughY) {
    if (V.clientW <= 0 || V.clientH <= 0) return;
    if (V.layDone || V.layY >= throughY) return;
    HDC hdc = GetDC(V.hwnd);
    FontsEnsure(hdc);
    int x0 = UI_Scale(28);
    int avail = V.clientW - UI_Scale(28) * 2 - UI_Scale(12);
    if (avail < UI_Scale(100)) avail = UI_Scale(100);
    while (V.layY < throughY) {
        if (!V.root || V.layIdx >= V.root->nChildren) {
            V.layDone = TRUE;
            break;
        }
        LayoutBlock(hdc, V.root->children[V.layIdx], x0, avail, &V.layY, 0);
        V.layIdx++;
    }
    ReleaseDC(V.hwnd, hdc);
    V.docH = DocHEstimate();
}

static void LayoutAll(void) {
    if (V.clientW <= 0 || V.clientH <= 0) {
        V.lastLayoutW = -1;
        return;
    }
    LayoutBegin();
    LayoutEnsure(V.scrollY + V.clientH * 2);
    if (V.scrollY > V.docH - V.clientH) {
        V.scrollY = V.docH - V.clientH;
        if (V.scrollY < 0) V.scrollY = 0;
    }
    InvalidateRect(V.hwnd, NULL, FALSE);
}

void MdTheme_Build(MdTheme* th) {
    if (g_dark) {
        th->bg        = RGB(30,30,30);
        th->fg        = RGB(212,216,221);
        th->fgMuted   = RGB(139,148,158);
        th->head      = RGB(230,237,243);
        th->link      = RGB(88,166,255);
        th->quoteBar  = RGB(77,87,98);
        th->codeBg    = RGB(40,42,46);
        th->codeBorder= RGB(60,63,68);
        th->codeFg    = RGB(230,220,200);
        th->synKw     = RGB(120,170,255);
        th->synStr    = RGB(224,150,110);
        th->synCom    = RGB(130,145,120);
        th->synNum    = RGB(220,190,120);
        th->synPre    = RGB(200,140,200);
        th->tableLine = RGB(60,63,68);
        th->tableHeadBg = RGB(44,46,51);
        th->hrule     = RGB(60,63,68);
        th->selBg     = RGB(36,38,42);
        th->merNodeFill  = RGB(47,52,60);
        th->merNodeBorder= RGB(110,120,135);
        th->merNodeText  = RGB(220,224,228);
        th->merEdge      = RGB(150,158,168);
        th->merLabelBg   = RGB(30,30,30);
        th->merLabelFg   = RGB(180,186,194);
        th->seqNoteBg    = RGB(72,66,42);
        th->seqNoteBorder= RGB(140,126,74);
        th->seqNoteFg    = RGB(235,228,205);
        th->frame        = RGB(96,104,116);
    } else {
        th->bg        = RGB(255,255,255);
        th->fg        = RGB(31,35,40);
        th->fgMuted   = RGB(110,119,129);
        th->head      = RGB(31,35,40);
        th->link      = RGB(9,105,218);
        th->quoteBar  = RGB(208,215,222);
        th->codeBg    = RGB(246,248,250);
        th->codeBorder= RGB(208,215,222);
        th->codeFg    = RGB(95,95,95);
        th->synKw     = RGB(0,80,200);
        th->synStr    = RGB(163,60,40);
        th->synCom    = RGB(100,120,90);
        th->synNum    = RGB(150,90,10);
        th->synPre    = RGB(140,60,160);
        th->tableLine = RGB(208,215,222);
        th->tableHeadBg = RGB(236,240,243);
        th->hrule     = RGB(208,215,222);
        th->selBg     = RGB(250,251,252);
        th->merNodeFill  = RGB(238,242,247);
        th->merNodeBorder= RGB(120,134,156);
        th->merNodeText  = RGB(31,35,40);
        th->merEdge      = RGB(110,119,129);
        th->merLabelBg   = RGB(255,255,255);
        th->merLabelFg   = RGB(90,98,106);
        th->seqNoteBg    = RGB(255,243,197);
        th->seqNoteBorder= RGB(222,203,122);
        th->seqNoteFg    = RGB(92,71,19);
        th->frame        = RGB(168,178,190);
    }
}

static int SbThickness(void) { return UI_Scale(12); }

static void PaintContent(HDC hdc, const MdTheme* th, BOOL drawSel) {
    RECT rcClient;
    GetClientRect(V.hwnd, &rcClient);
    int top = V.scrollY, bot = V.scrollY + V.clientH;

    SetBkMode(hdc, TRANSPARENT);

    HPEN ulPen = CreatePen(PS_SOLID, 1, th->link);
    HPEN strikePen = CreatePen(PS_SOLID, 1, th->fgMuted);
    HPEN framePen = CreatePen(PS_SOLID, 1, th->codeBorder);
    HBRUSH codeBgBr = CreateSolidBrush(th->codeBg);
    HBRUSH quoteBr = CreateSolidBrush(th->quoteBar);
    HBRUSH hruleBr = CreateSolidBrush(th->hrule);
    HBRUSH canvasBr = CreateSolidBrush(th->selBg);
    HBRUSH selBr = NULL;
    if (drawSel)
        selBr = CreateSolidBrush(g_dark ? RGB(64,96,160) : RGB(0,120,215));

    for (int i = 0; i < V.nItems; i++) {
        MdItem* it = &V.items[i];
        if (it->rc.top >= bot || it->rc.bottom <= top) continue;
        switch (it->type) {
            case ITM_RECT: {
                HBRUSH br = NULL;
                switch (it->flag) {
                    case RCT_CODEBG: case RCT_TABLEHEAD: br = codeBgBr; break;
                    case RCT_QUOTEBAR: br = quoteBr; break;
                    case RCT_HRULE: br = hruleBr; break;
                    case RCT_CANVAS: br = canvasBr; break;
                }
                if (br) {
                    RECT rc = it->rc;
                    OffsetRect(&rc, 0, -V.scrollY);
                    FillRect(hdc, &rc, br);
                }
                break;
            }
            case ITM_FRAME: {
                RECT rc = it->rc;
                OffsetRect(&rc, 0, -V.scrollY);
                HPEN op = (HPEN)SelectObject(hdc, framePen);
                HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, ob);
                SelectObject(hdc, op);
                break;
            }
            case ITM_DIAGRAM:
                if (it->diag) {
                    RECT drc = it->rc;
                    InflateRect(&drc, UI_Scale(16), UI_Scale(16));
                    OffsetRect(&drc, 0, -V.scrollY);
                    SaveDC(hdc);
                    IntersectClipRect(hdc, drc.left, drc.top, drc.right, drc.bottom);
                    Mermaid_Draw(it->diag, hdc, it->rc.left, it->rc.top - V.scrollY,
                                 &V.fonts, th);
                    RestoreDC(hdc, -1);
                }
                break;
            case ITM_IMAGE: {
                if (!s_gdipOk || !it->image) break;
                RECT rc = it->rc;
                OffsetRect(&rc, 0, -V.scrollY);
                int dw = rc.right - rc.left, dh = rc.bottom - rc.top;
                if (dw <= 0 || dh <= 0) break;
                GpGraphics* g = NULL;
                if (p_GdipCreateFromHDC(hdc, &g) == 0 && g) {
                    GpImage* draw = (GpImage*)it->image;
                    UINT sw = it->imageW, sh = it->imageH;
                    int interp = 7 /*HighQualityBicubic*/;
                    if (sw != (UINT)dw || sh != (UINT)dh) {
                        /* 显示尺寸≠原始尺寸：首帧按显示尺寸预渲染一次，
                           之后滚动每帧只做同尺寸搬运，不再整图重采样 */
                        if (!it->thumb || it->thumbW != (UINT)dw || it->thumbH != (UINT)dh) {
                            if (it->thumb) p_GdipDisposeImage((GpImage*)it->thumb);
                            it->thumb = NULL;
                            if (dw <= 8192 && dh <= 8192 &&
                                p_GdipCreateBitmapFromGraphics &&
                                p_GdipGetImageGraphicsContext) {
                                GpImage* bmp = NULL;
                                if (p_GdipCreateBitmapFromGraphics(dw, dh, g, &bmp) == 0 && bmp) {
                                    GpGraphics* bg = NULL;
                                    if (p_GdipGetImageGraphicsContext(bmp, &bg) == 0 && bg) {
                                        p_GdipSetInterpolationMode(bg, 7);
                                        p_GdipDrawImageRectRectI(bg, draw,
                                            0, 0, dw, dh, 0, 0, (int)sw, (int)sh,
                                            2, NULL, NULL, NULL);
                                        p_GdipDeleteGraphics(bg);
                                        it->thumb = bmp;
                                        it->thumbW = (UINT)dw;
                                        it->thumbH = (UINT)dh;
                                    } else {
                                        p_GdipDisposeImage(bmp);
                                    }
                                }
                            }
                        }
                        if (it->thumb) {
                            draw = (GpImage*)it->thumb;
                            sw = it->thumbW;
                            sh = it->thumbH;
                            interp = 5 /*NearestNeighbor：同尺寸搬运*/;
                        }
                    }
                    p_GdipSetInterpolationMode(g, interp);
                    p_GdipDrawImageRectRectI(g, draw,
                        rc.left, rc.top, dw, dh, 0, 0, (int)sw, (int)sh,
                        2, NULL, NULL, NULL);
                    p_GdipFlush(g, 1);
                    p_GdipDeleteGraphics(g);
                }
                break;
            }
            case ITM_MATH: {
                if (it->mathRun && it->mathRun->math) {
                    if (selBr && SelPointCovered(i, 0, 0)) {
                        RECT sr = it->rc;
                        OffsetRect(&sr, 0, -V.scrollY);
                        FillRect(hdc, &sr, selBr);
                    }
                    Math_Draw(it->mathRun->math, hdc,
                              it->rc.left, it->rc.top + Math_Ascent(it->mathRun->math) - V.scrollY,
                              th, &V.fonts);
                }
                break;
            }
            case ITM_LINES: {
                for (int l = 0; l < it->nLines; l++) {
                    MdLine* ln = &it->lines[l];
                    int lineTop = it->rc.top + ln->y;
                    int lineBot = lineTop + ln->h;
                    if (lineTop >= bot || lineBot <= top) continue;
                    int baseY = it->rc.top + ln->y + ln->asc - V.scrollY;
                    for (int k = 0; k < ln->nF; k++) {
                        MdFrag* fr = &ln->frags[k];
                        MdRun* run = fr->run;
                        if ((run->style & STY_MATH) && run->math) {
                            int fx2 = it->rc.left + fr->x;
                            if (selBr && SelPointCovered(i, l, k)) {
                                RECT mr = { fx2, baseY - ln->asc,
                                            fx2 + (fr->w > 0 ? fr->w : UI_Scale(12)),
                                            baseY - ln->asc + ln->h };
                                FillRect(hdc, &mr, selBr);
                            }
                            Math_Draw(run->math, hdc, fx2, baseY, th, &V.fonts);
                            continue;
                        }
                        if (!fr->s || fr->len == 0) continue;
                        int fx = it->rc.left + fr->x;
                        HFONT f = FontFor(run->style, it->role);
                        HFONT of = (HFONT)SelectObject(hdc, f);
                        if ((run->style & STY_CODE) && it->role == ROLE_BODY) {
                            RECT chip = { fx - 3, baseY - ln->asc,
                                          fx + fr->w + 3, baseY - ln->asc + ln->h };
                            FillRect(hdc, &chip, codeBgBr);
                            SetTextColor(hdc, th->codeFg);
                        } else if (run->style & STY_LINK) {
                            SetTextColor(hdc, th->link);
                        } else if (run->style & STY_IMG) {
                            SetTextColor(hdc, th->link);
                        } else if (it->role >= ROLE_H1 && it->role <= ROLE_H1 + 5) {
                            SetTextColor(hdc, th->head);
                        } else if (it->role == ROLE_MONO) {
                            if (run->style & STY_COM)      SetTextColor(hdc, th->synCom);
                            else if (run->style & STY_KW)   SetTextColor(hdc, th->synKw);
                            else if (run->style & STY_STR)  SetTextColor(hdc, th->synStr);
                            else if (run->style & STY_NUM)  SetTextColor(hdc, th->synNum);
                            else if (run->style & STY_PRE)  SetTextColor(hdc, th->synPre);
                            else                            SetTextColor(hdc, th->codeFg);
                        } else {
                            SetTextColor(hdc, th->fg);
                        }
                        int sa = 0, sb = 0;
                        SIZE za = { 0, 0 }, zb = { 0, 0 };
                        if (selBr) {
                            SelFragRange(i, l, k, fr->len, &sa, &sb);
                            if (sb > sa) {
                                GetTextExtentPoint32W(hdc, fr->s, sa, &za);
                                GetTextExtentPoint32W(hdc, fr->s, sb, &zb);
                                RECT sr = { fx + za.cx, baseY - ln->asc,
                                            fx + zb.cx, baseY - ln->asc + ln->h };
                                FillRect(hdc, &sr, selBr);
                            } else {
                                sa = sb = 0;
                            }
                        }
                        SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
                        if (sb > sa) {
                            /* 部分选中分三段画：未选中段须保持原色，
                               整段一次 TextOut 会把它也反白 */
                            if (sa > 0) TextOutW(hdc, fx, baseY, fr->s, sa);
                            SetTextColor(hdc, RGB(255,255,255));
                            TextOutW(hdc, fx + za.cx, baseY, fr->s + sa, sb - sa);
                            if (sb < fr->len)
                                TextOutW(hdc, fx + zb.cx, baseY, fr->s + sb,
                                         fr->len - sb);
                        } else {
                            TextOutW(hdc, fx, baseY, fr->s, fr->len);
                        }
                        if (run->style & STY_LINK) {
                            HPEN op = (HPEN)SelectObject(hdc, ulPen);
                            MoveToEx(hdc, fx, baseY + 1, NULL);
                            LineTo(hdc, fx + fr->w, baseY + 1);
                            SelectObject(hdc, op);
                        }
                        if (run->style & STY_STRIKE) {
                            HPEN op = (HPEN)SelectObject(hdc, strikePen);
                            int midy = baseY - ln->asc / 2;
                            MoveToEx(hdc, fx, midy, NULL);
                            LineTo(hdc, fx + fr->w, midy);
                            SelectObject(hdc, op);
                        }
                        SelectObject(hdc, of);
                    }
                }
                SetTextAlign(hdc, TA_LEFT | TA_TOP);
                break;
            }
        }
    }
    DeleteObject(ulPen);
    DeleteObject(strikePen);
    DeleteObject(framePen);
    DeleteObject(codeBgBr);
    DeleteObject(quoteBr);
    DeleteObject(hruleBr);
    DeleteObject(canvasBr);
    if (selBr) DeleteObject(selBr);
}

static void PaintScrollbar(HDC hdc, const MdTheme* th) {
    int sbw = SbThickness();
    if (V.docH <= V.clientH) return;
    int trackX = V.clientW - sbw;
    COLORREF trackClr = g_dark ? RGB(37,37,40) : RGB(240,241,242);
    COLORREF thumbClr = g_dark ? RGB(82,86,93) : RGB(193,198,203);
    RECT tr = { trackX, 0, V.clientW, V.clientH };
    HBRUSH br = CreateSolidBrush(trackClr);
    FillRect(hdc, &tr, br);
    DeleteObject(br);
    int thumbH = V.clientH * V.clientH / V.docH;
    if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
    int maxScroll = V.docH - V.clientH;
    int thumbY = V.scrollY * (V.clientH - thumbH) / maxScroll;
    RECT thr = { trackX + 2, thumbY, V.clientW - 2, thumbY + thumbH };
    br = CreateSolidBrush(thumbClr);
    FillRect(hdc, &thr, br);
    DeleteObject(br);
    (void)th;
}

static struct {
    HDC dc; HBITMAP bmp; HBITMAP defBmp; int w, h;
} s_mem;

static void MemFree(void) {
    if (s_mem.dc) {
        if (s_mem.bmp) {
            SelectObject(s_mem.dc, s_mem.defBmp);
            DeleteObject(s_mem.bmp);
        }
        DeleteDC(s_mem.dc);
    }
    ZeroMemory(&s_mem, sizeof(s_mem));
}

static HDC MemDCFor(HDC src, int w, int h) {
    if (s_mem.dc && s_mem.w == w && s_mem.h == h) return s_mem.dc;
    MemFree();
    s_mem.dc = CreateCompatibleDC(src);
    if (!s_mem.dc) return NULL;
    s_mem.defBmp = (HBITMAP)GetCurrentObject(s_mem.dc, OBJ_BITMAP);
    s_mem.bmp = CreateCompatibleBitmap(src, w, h);
    if (!s_mem.bmp) { DeleteDC(s_mem.dc); s_mem.dc = NULL; return NULL; }
    SelectObject(s_mem.dc, s_mem.bmp);
    s_mem.w = w; s_mem.h = h;
    return s_mem.dc;
}

static void Paint(void) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(V.hwnd, &ps);
    LayoutEnsure(V.scrollY + V.clientH * 2);   /* 滚入未布局区域时按需延伸 */
    RECT rc;
    GetClientRect(V.hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(V.hwnd, &ps); return; }

    HDC mem = MemDCFor(hdc, w, h);
    if (!mem) { EndPaint(V.hwnd, &ps); return; }

    MdTheme th;
    MdTheme_Build(&th);
    HBRUSH bg = CreateSolidBrush(th.bg);
    RECT full = { 0, 0, w, h };
    FillRect(mem, &full, bg);
    DeleteObject(bg);

    if (!V.fontsOk) { HDC tmp = GetDC(V.hwnd); FontsEnsure(tmp); ReleaseDC(V.hwnd, tmp); }
    PaintContent(mem, &th, TRUE);
    PaintScrollbar(mem, &th);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    EndPaint(V.hwnd, &ps);
}

static void ClampScroll(void) {
    int maxScroll = V.docH - V.clientH;
    if (maxScroll < 0) maxScroll = 0;
    if (V.scrollY < 0) V.scrollY = 0;
    if (V.scrollY > maxScroll) V.scrollY = maxScroll;
}

static void ScrollBy(int dy) {
    int old = V.scrollY;
    V.scrollY += dy;
    ClampScroll();
    if (V.scrollY != old) {
        InvalidateRect(V.hwnd, NULL, FALSE);
        if (g_mdSplit && !g_mdSyncLock) {
            g_mdSyncLock = TRUE;
            Editor_SyncScrollFromPreview(MdView_GetScrollFraction());
            g_mdSyncLock = FALSE;
        }
    }
}

static const wchar_t* HitLink(int px, int py) {
    int dy = py + V.scrollY;
    int dx = px;
    for (int i = 0; i < V.nItems; i++) {
        MdItem* it = &V.items[i];
        if (it->type == ITM_IMAGE) {
            if (it->href && dy >= it->rc.top && dy < it->rc.bottom &&
                dx >= it->rc.left && dx < it->rc.right)
                return it->href;
            continue;
        }
        if (it->type != ITM_LINES) continue;
        if (dy < it->rc.top || dy >= it->rc.bottom) continue;
        for (int l = 0; l < it->nLines; l++) {
            MdLine* ln = &it->lines[l];
            int lt = it->rc.top + ln->y;
            if (dy < lt || dy >= lt + ln->h) continue;
            for (int k = 0; k < ln->nF; k++) {
                MdFrag* fr = &ln->frags[k];
                if ((!fr->run->href && !fr->run->link) || fr->len == 0) continue;
                int fx = it->rc.left + fr->x;
                if (dx >= fx - 1 && dx <= fx + fr->w + 1)
                    return fr->run->link ? fr->run->link : fr->run->href;
            }
        }
    }
    return NULL;
}

static void OpenLink(const wchar_t* href) {
    if (!href || !*href) return;
    if (href[0] == L'#') return;
    if (_wcsnicmp(href, L"http://", 7) == 0 || _wcsnicmp(href, L"https://", 8) == 0 ||
        _wcsnicmp(href, L"mailto:", 7) == 0) {
        ShellExecuteW(V.hwnd, L"open", href, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    wchar_t full[MAX_PATH];
    if (ResolveLocalHref(href, full, MAX_PATH))
        ShellExecuteW(V.hwnd, L"open", full, NULL, NULL, SW_SHOWNORMAL);
}

/* ===================== 文本选区与复制 ===================== */

static BOOL SelHasText(void) {
    return V.selActive && PosCmp(V.selAnchor, V.selCaret) != 0;
}

/* 选区无向区间：anchor→caret 可能反向 */
static void SelBounds(MdPos* s, MdPos* e) {
    *s = V.selAnchor; *e = V.selCaret;
    if (PosCmp(*s, *e) > 0) { MdPos t = *s; *s = *e; *e = t; }
}

/* 零宽位置（行内公式）是否被选区跨过 */
static BOOL SelPointCovered(int item, int line, int frag) {
    if (!V.selActive) return FALSE;
    MdPos s, e;
    SelBounds(&s, &e);
    MdPos p = { item, line, frag, 0 };
    return PosCmp(s, p) <= 0 && PosCmp(e, p) > 0;
}

/* 片段内被选中的字符区间 [a,b) */
static void SelFragRange(int item, int line, int frag, int len, int* a, int* b) {
    *a = *b = 0;
    if (!V.selActive) return;
    MdPos s, e;
    SelBounds(&s, &e);
    MdPos fs = { item, line, frag, 0 };
    MdPos fe = { item, line, frag, len };
    if (PosCmp(e, fs) <= 0 || PosCmp(s, fe) >= 0) return;
    *a = PosCmp(s, fs) <= 0 ? 0 : s.ch;
    *b = PosCmp(e, fe) >= 0 ? len : e.ch;
}

/* 片段内偏移 off 处的字符序号（前缀宽度二分，与布局同字体度量） */
static int ChAtX(HDC hdc, MdItem* it, MdFrag* fr, int off) {
    if (fr->len <= 0 || !fr->s || off <= 0) return 0;
    HFONT of = (HFONT)SelectObject(hdc, FontFor(fr->run->style, it->role));
    int lo = 0, hi = fr->len;
    while (lo < hi) {              /* 最小 k：width(s[0..k)) >= off */
        int mid = lo + (hi - lo) / 2;
        SIZE sz;
        GetTextExtentPoint32W(hdc, fr->s, mid, &sz);
        if (sz.cx < off) lo = mid + 1;
        else hi = mid;
    }
    SelectObject(hdc, of);
    return lo;
}

/* 客户区坐标 → 最近文本位置；空隙/空白吸附相邻行，保证跨行拖选顺滑 */
static BOOL HitTextPos(int px, int py, MdPos* out) {
    if (!V.nItems) return FALSE;
    HDC hdc = GetDC(V.hwnd);
    if (!hdc) return FALSE;
    if (!V.fontsOk) FontsEnsure(hdc);
    int dy = py + V.scrollY;
    int dx = px;
    int best = -1, bestV = 0, bestH = 0;
    for (int i = 0; i < V.nItems; i++) {
        MdItem* it = &V.items[i];
        if (it->type != ITM_LINES && it->type != ITM_MATH) continue;
        int dv, dh;
        if (dy < it->rc.top) dv = it->rc.top - dy;
        else if (dy >= it->rc.bottom) dv = dy - it->rc.bottom + 1;
        else dv = 0;
        if (dx < it->rc.left) dh = it->rc.left - dx;
        else if (dx >= it->rc.right) dh = dx - it->rc.right + 1;
        else dh = 0;
        /* 垂直优先，水平打破平局：同一垂直带有多个并排项（同行表格
           单元格、列表符号与正文首行），只看垂直会永远命中最前一个 */
        if (best < 0 || dv < bestV || (dv == bestV && dh < bestH)) {
            best = i; bestV = dv; bestH = dh;
        }
    }
    if (best < 0) { ReleaseDC(V.hwnd, hdc); return FALSE; }
    MdItem* it = &V.items[best];
    if (it->type == ITM_MATH) {    /* 整块公式：左右半区 = 起点/终点 */
        out->item = best; out->line = 0; out->frag = 0;
        out->ch = (dx < (it->rc.left + it->rc.right) / 2) ? 0 : 1;
        ReleaseDC(V.hwnd, hdc);
        return TRUE;
    }
    int bl = 0, blDist = -1;
    for (int l = 0; l < it->nLines; l++) {
        MdLine* ln = &it->lines[l];
        int top = it->rc.top + ln->y, bot = top + ln->h;
        int d;
        if (dy < top) d = top - dy;
        else if (dy >= bot) d = dy - bot + 1;
        else d = 0;
        if (blDist < 0 || d < blDist) { bl = l; blDist = d; }
    }
    MdLine* ln = &it->lines[bl];
    out->item = best; out->line = bl;
    int kf = -1;
    for (int k = 0; k < ln->nF; k++) {
        MdFrag* fr = &ln->frags[k];
        if (fr->len == 0 && !(fr->run->style & STY_MATH)) continue;
        kf = k;                    /* 记住末个有效片段（越过末段时用） */
        if (dx < it->rc.left + fr->x + (fr->w > 0 ? fr->w : 1)) break;
    }
    if (kf < 0) {
        out->frag = 0; out->ch = 0;
    } else {
        out->frag = kf;
        MdFrag* fr = &ln->frags[kf];
        out->ch = (fr->len > 0)
            ? ChAtX(hdc, it, fr, dx - (it->rc.left + fr->x)) : 0;
    }
    ReleaseDC(V.hwnd, hdc);
    return TRUE;
}

/* 全选：补全布局后取首/末文本位置 */
static void SelectAllDoc(void) {
    LayoutEnsure(0x7FFFFFFF);
    if (!V.nItems) return;
    MdPos a = { 0, 0, 0, 0 };
    MdPos b = a;
    for (int i = V.nItems - 1; i >= 0; i--) {
        MdItem* it = &V.items[i];
        if (it->type == ITM_LINES && it->nLines > 0) {
            MdLine* ln = &it->lines[it->nLines - 1];
            b.item = i; b.line = it->nLines - 1; b.frag = ln->nF; b.ch = 0;
            break;
        }
        if (it->type == ITM_MATH) {
            b.item = i; b.line = 0; b.frag = 0; b.ch = 1;
            break;
        }
    }
    if (PosCmp(a, b) == 0) return;
    V.selAnchor = a; V.selCaret = b; V.selActive = TRUE;
    InvalidateRect(V.hwnd, NULL, FALSE);
}

static BOOL AppEnd(wchar_t** buf, int* len, int* cap, const wchar_t* s, int n) {
    if (n <= 0 || !s) return TRUE;
    if (*len + n + 1 > *cap) {
        int nc = *cap;
        while (nc < *len + n + 1) nc *= 2;
        wchar_t* nb = (wchar_t*)realloc(*buf, (size_t)nc * sizeof(wchar_t));
        if (!nb) return FALSE;
        *buf = nb; *cap = nc;
    }
    memcpy(*buf + *len, s, (size_t)n * sizeof(wchar_t));
    *len += n;
    return TRUE;
}

/* ---- 复制为 Markdown：行内标记按 run 边界开合 ----
   同一 run 的多个片段只开/合一组标记；开合顺序互为镜像，链接在最外层。 */
static void MdInlineOpen(wchar_t** buf, int* len, int* cap, MdRun* run) {
    unsigned st = run->style;
    if ((st & STY_IMG) && run->href && run->href[0])
        AppEnd(buf, len, cap, L"![", 2);
    else if ((st & STY_LINK) && run->href && run->href[0])
        AppEnd(buf, len, cap, L"[", 1);
    if (st & STY_STRIKE) AppEnd(buf, len, cap, L"~~", 2);
    if (st & STY_BOLD)   AppEnd(buf, len, cap, L"**", 2);
    if (st & STY_EM)     AppEnd(buf, len, cap, L"*", 1);
    if (st & STY_CODE)   AppEnd(buf, len, cap, L"`", 1);
}

static void MdInlineClose(wchar_t** buf, int* len, int* cap, MdRun* run) {
    unsigned st = run->style;
    if (st & STY_CODE)   AppEnd(buf, len, cap, L"`", 1);
    if (st & STY_EM)     AppEnd(buf, len, cap, L"*", 1);
    if (st & STY_BOLD)   AppEnd(buf, len, cap, L"**", 2);
    if (st & STY_STRIKE) AppEnd(buf, len, cap, L"~~", 2);
    if (((st & STY_IMG) || (st & STY_LINK)) && run->href && run->href[0]) {
        AppEnd(buf, len, cap, L"](", 2);
        AppEnd(buf, len, cap, run->href, (int)wcslen(run->href));
        AppEnd(buf, len, cap, L")", 1);
    }
}

/* 列表符号项：ownRun 独字、无样式、role 为正文、内容为 •/☑/☐/N. 之一
   （代码块 ownRun 是哑头 run、style 也为 0，必须用 role 排除） */
static BOOL IsListMarkItem(MdItem* it) {
    return it->type == ITM_LINES && it->role == ROLE_BODY && it->ownRun &&
           it->nLines == 1 && it->ownRun->style == 0 && it->ownRun->text &&
           (it->ownRun->text[0] == L'•' || it->ownRun->text[0] == L'☑' ||
            it->ownRun->text[0] == L'☐' ||
            (it->ownRun->text[0] >= L'0' && it->ownRun->text[0] <= L'9'));
}

static const wchar_t* MdListMarkText(const wchar_t* m) {
    if (m[0] == L'☑') return L"- [x]";
    if (m[0] == L'☐') return L"- [ ]";
    if (m[0] == L'•') return L"-";
    return m;             /* "1."/"1)" 本身就是合法 Markdown 标记 */
}

/* 选区 → Markdown 文本：链接/强调/行内码/公式重建语法，标题补 # 前缀，
   列表符号转回 Markdown 标记；可视行以 \n 连接；并排项（同行表格单元
   格）以 \t 连接保持列结构；代码块内容保持纯文本 */
static wchar_t* BuildSelText(void) {
    if (!SelHasText()) return NULL;
    MdPos s, e;
    SelBounds(&s, &e);
    int cap = 4096, len = 0;
    wchar_t* buf = (wchar_t*)malloc((size_t)cap * sizeof(wchar_t));
    if (!buf) return NULL;
    int prevTop = 0, prevBot = 0;
    BOOL havePrev = FALSE, prevWasMark = FALSE;
    for (int i = 0; i < V.nItems; i++) {
        MdItem* it = &V.items[i];
        MdPos is = { i, 0, 0, 0 }, ie = { i + 1, 0, 0, 0 };
        if (PosCmp(e, is) <= 0) break;             /* 选区到此为止 */
        if (PosCmp(s, ie) >= 0) continue;
        if (it->type == ITM_MATH) {
            if (it->mathRun && it->mathRun->text && PosCmp(s, is) <= 0) {
                AppEnd(&buf, &len, &cap, L"$$", 2);
                AppEnd(&buf, &len, &cap, it->mathRun->text,
                       (int)wcslen(it->mathRun->text));
                AppEnd(&buf, &len, &cap, L"$$", 2);
                AppEnd(&buf, &len, &cap, L"\n", 1);
                prevTop = it->rc.top; prevBot = it->rc.bottom;
                havePrev = TRUE; prevWasMark = FALSE;
            }
            continue;
        }
        if (it->type != ITM_LINES || it->nLines == 0) continue;
        MdLine* lastLn = &it->lines[it->nLines - 1];
        int curTop = it->rc.top + it->lines[0].y;
        int curBot = it->rc.top + lastLn->y + lastLn->h;
        if (havePrev && curTop < prevBot && prevTop < curBot &&
            len > 0 && buf[len - 1] == L'\n') {
            len--;          /* 并排：表格单元格以 \t、列表符号后以空格连接 */
            AppEnd(&buf, &len, &cap, prevWasMark ? L" " : L"\t", 1);
        }
        BOOL wasMark = FALSE;
        if (IsListMarkItem(it) && PosCmp(s, is) <= 0) {
            AppEnd(&buf, &len, &cap, MdListMarkText(it->ownRun->text),
                   (int)wcslen(MdListMarkText(it->ownRun->text)));
            AppEnd(&buf, &len, &cap, L"\n", 1);
            wasMark = TRUE;
        } else {
            if (it->role >= ROLE_H1 && it->role <= ROLE_H1 + 5 &&
                PosCmp(s, is) <= 0) {              /* 标题补 # 前缀 */
                for (int h = 0; h < it->role; h++)
                    AppEnd(&buf, &len, &cap, L"#", 1);
                AppEnd(&buf, &len, &cap, L" ", 1);
            }
            MdRun* curRun = NULL;
            for (int l = 0; l < it->nLines; l++) {
                MdLine* ln = &it->lines[l];
                MdPos ls = { i, l, 0, 0 }, le = { i, l + 1, 0, 0 };
                if (PosCmp(e, ls) <= 0) break;
                if (PosCmp(s, le) >= 0) continue;
                for (int k = 0; k < ln->nF; k++) {
                    MdFrag* fr = &ln->frags[k];
                    if (fr->len == 0) {
                        if ((fr->run->style & STY_MATH) && fr->run->text &&
                            SelPointCovered(i, l, k)) {
                            if (curRun) {
                                MdInlineClose(&buf, &len, &cap, curRun);
                                curRun = NULL;
                            }
                            int dl = (fr->run->style & STY_MATHDISP) ? 2 : 1;
                            AppEnd(&buf, &len, &cap, dl == 2 ? L"$$" : L"$", dl);
                            AppEnd(&buf, &len, &cap, fr->run->text,
                                   (int)wcslen(fr->run->text));
                            AppEnd(&buf, &len, &cap, dl == 2 ? L"$$" : L"$", dl);
                        }
                        continue;
                    }
                    int a, b;
                    SelFragRange(i, l, k, fr->len, &a, &b);
                    if (b > a) {
                        if (fr->run != curRun) {   /* run 边界开合标记 */
                            if (curRun)
                                MdInlineClose(&buf, &len, &cap, curRun);
                            MdInlineOpen(&buf, &len, &cap, fr->run);
                            curRun = fr->run;
                        }
                        AppEnd(&buf, &len, &cap, fr->s + a, b - a);
                    }
                }
                AppEnd(&buf, &len, &cap, L"\n", 1);
            }
            if (curRun) MdInlineClose(&buf, &len, &cap, curRun);
        }
        prevTop = curTop; prevBot = curBot;
        havePrev = TRUE; prevWasMark = wasMark;
    }
    while (len > 0 && buf[len - 1] == L'\n') len--;
    if (len == 0) { free(buf); return NULL; }
    buf[len] = L'\0';
    return buf;
}

static void DoCopySelection(void) {
    wchar_t* txt = BuildSelText();
    if (!txt) return;
    if (OpenClipboard(V.hwnd)) {
        EmptyClipboard();
        size_t bytes = ((size_t)wcslen(txt) + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        BOOL ok = FALSE;
        if (h) {
            wchar_t* p = (wchar_t*)GlobalLock(h);
            if (p) {
                memcpy(p, txt, bytes);
                GlobalUnlock(h);
                ok = (SetClipboardData(CF_UNICODETEXT, h) != NULL);
            }
            if (!ok) GlobalFree(h);
        }
        CloseClipboard();
    }
    free(txt);
}

enum { MDVC_COPY = 1, MDVC_SELALL = 2 };   /* 预览私有菜单命令 */

static void ShowContextMenu(HWND hwnd, LPARAM lp) {
    POINT pt;
    if (lp == (LPARAM)-1) {         /* 键盘触发（Shift+F10）：客户区中心 */
        RECT rc;
        GetClientRect(hwnd, &rc);
        pt.x = (rc.left + rc.right) / 2;
        pt.y = (rc.top + rc.bottom) / 2;
        ClientToScreen(hwnd, &pt);
    } else {
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
    }
    HMENU m = CreatePopupMenu();
    UINT on = MF_STRING, off = MF_STRING | MF_GRAYED;
    I18n_OwnerAppend(m, SelHasText() ? on : off, MDVC_COPY, T(STR_ITEM_COPY));
    I18n_OwnerAppend(m, on, MDVC_SELALL, T(STR_ITEM_SELECTALL));
    I18n_ApplyMenuTheme(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (cmd == MDVC_COPY) DoCopySelection();
    else if (cmd == MDVC_SELALL) SelectAllDoc();
}

static void LoadContentEx(int index, BOOL force);

static LRESULT CALLBACK MdViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (V.imgTimerOn) {
                KillTimer(hwnd, TID_MDIMG);
                V.imgTimerOn = FALSE;
            }
            MemFree();
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            V.clientW = LOWORD(lp);
            V.clientH = HIWORD(lp);
            if (V.clientW != V.lastLayoutW) LayoutAll();
            else { ClampScroll(); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        case WM_MOUSEWHEEL: {
            static int acc = 0;
            acc += (short)HIWORD(wp);
            int steps = acc / WHEEL_DELTA;
            acc %= WHEEL_DELTA;
            int line = V.fonts.lineH * 3;
            if (wp & MK_SHIFT) line *= 3;
            if (steps) ScrollBy(-steps * line);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            int sbw = SbThickness();
            if (V.docH > V.clientH && px >= V.clientW - sbw) {
                int thumbH = V.clientH * V.clientH / V.docH;
                if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
                int maxScroll = V.docH - V.clientH;
                int thumbY = maxScroll ? V.scrollY * (V.clientH - thumbH) / maxScroll : 0;
                if (py >= thumbY && py < thumbY + thumbH) {
                    V.sbDrag = TRUE;
                    V.sbDragOff = py - thumbY;
                    SetCapture(hwnd);
                } else {
                    ScrollBy((py < thumbY ? -1 : 1) * V.clientH * 9 / 10);
                }
                return 0;
            }
            /* 选区优先：按下即锚定；链接记下，未拖开时抬起才打开 */
            MdPos pos;
            BOOL havePos = HitTextPos(px, py, &pos);
            const wchar_t* href = HitLink(px, py);
            if (V.pendLink) { free(V.pendLink); V.pendLink = NULL; }
            if (href) V.pendLink = _wcsdup(href);
            V.downPt.x = px; V.downPt.y = py;
            if (havePos) {
                if ((GetKeyState(VK_SHIFT) & 0x8000) && V.selActive) {
                    V.selCaret = pos;               /* Shift+点击：扩展选区 */
                } else {
                    V.selAnchor = pos;
                    V.selCaret = pos;
                }
                V.selActive = TRUE;
            }
            V.selDrag = TRUE;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            if (V.sbDrag && V.docH > V.clientH) {
                int thumbH = V.clientH * V.clientH / V.docH;
                if (thumbH < UI_Scale(24)) thumbH = UI_Scale(24);
                int t = py - V.sbDragOff;
                int maxScroll = V.docH - V.clientH;
                V.scrollY = maxScroll * t / (V.clientH - thumbH);
                ClampScroll();
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_mdSplit && !g_mdSyncLock) {
                    g_mdSyncLock = TRUE;
                    double achieved = Editor_SyncScrollFromPreview(MdView_GetScrollFraction());
                    g_mdSyncLock = FALSE;
                    if (achieved >= 0.0) {
                        int ms = V.docH - V.clientH;
                        if (ms > 0) {
                            int y2 = (int)(achieved * ms + 0.5);
                            if (y2 < 0) y2 = 0;
                            if (y2 > ms) y2 = ms;
                            if (y2 != V.scrollY) {
                                V.scrollY = y2;
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                        }
                    }
                }
                return 0;
            }
            if (V.selDrag) {
                /* 拖近上下缘自动滚动；坐标吸回客户区再命中 */
                int edge = UI_Scale(24);
                if (py < edge && V.scrollY > 0) ScrollBy(-V.fonts.lineH * 2);
                else if (py >= V.clientH - edge && V.scrollY < V.docH - V.clientH)
                    ScrollBy(V.fonts.lineH * 2);
                int mx = px < 0 ? 0 : (px >= V.clientW ? V.clientW - 1 : px);
                int my = py < 0 ? 0 : (py >= V.clientH ? V.clientH - 1 : py);
                MdPos pos;
                if (HitTextPos(mx, my, &pos) && PosCmp(pos, V.selCaret) != 0) {
                    V.selCaret = pos;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
            const wchar_t* href = HitLink(px, py);
            SetCursor(href ? V.hHand : LoadCursorW(NULL, IDC_ARROW));
            return 0;
        }
        case WM_LBUTTONUP: {
            if (V.sbDrag) {
                V.sbDrag = FALSE;
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            /* 重排可能已把 selDrag 复位，捕获仍需解除 */
            if (V.selDrag || GetCapture() == hwnd) {
                V.selDrag = FALSE;
                if (GetCapture() == hwnd) ReleaseCapture();
                if (V.pendLink) {
                    int thr = GetSystemMetrics(SM_CXDRAG);
                    int ty = GetSystemMetrics(SM_CYDRAG);
                    if (thr < ty) thr = ty;
                    int dxp = GET_X_LPARAM(lp) - V.downPt.x;
                    int dyp = GET_Y_LPARAM(lp) - V.downPt.y;
                    if (dxp * dxp + dyp * dyp <= thr * thr)
                        OpenLink(V.pendLink);
                    free(V.pendLink);
                    V.pendLink = NULL;
                }
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (V.selDrag) {
                V.selDrag = FALSE;
                if (V.pendLink) { free(V.pendLink); V.pendLink = NULL; }
            }
            return 0;
        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd, lp);
            return 0;
        case WM_KEYDOWN:
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wp == 'C' || wp == 'c' || wp == VK_INSERT) {
                    if (!SelHasText()) SelectAllDoc();   /* 无选区：复制全文 */
                    DoCopySelection();
                    return 0;
                }
                if (wp == 'A' || wp == 'a') {
                    SelectAllDoc();
                    return 0;
                }
            }
            switch (wp) {
                case VK_ESCAPE:
                    if (SelHasText()) {   /* 先清选区，再次按下才退出预览 */
                        V.selActive = FALSE;
                        V.selCaret = V.selAnchor;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (g_mdSplit) {
                        if (g_curDoc >= 0 && g_curDoc < g_docCount &&
                            g_docs[g_curDoc].hwndEdit)
                            SetFocus(g_docs[g_curDoc].hwndEdit);
                    } else {
                        PostMessageW(g_hwndMain, WM_COMMAND, MAKEWPARAM(IDM_VIEW_MD, 0), 0);
                    }
                    return 0;
                case VK_UP:    ScrollBy(-V.fonts.lineH); return 0;
                case VK_DOWN:  ScrollBy(V.fonts.lineH); return 0;
                case VK_PRIOR: ScrollBy(-V.clientH * 9 / 10); return 0;
                case VK_NEXT:  ScrollBy(V.clientH * 9 / 10); return 0;
                case VK_HOME:  ScrollBy(-V.docH); return 0;
                case VK_END:
                    LayoutEnsure(0x7FFFFFFF);   /* 补全布局拿到精确 docH 再到底 */
                    ScrollBy(V.docH); return 0;
            }
            break;
        case WM_MDIMG_READY:
            /* 多张外链图片的下载完成消息常成批到达；每条都触发整篇强制重排
               会形成重算风暴。120ms 合并窗口内只重排一次。 */
            if (!V.imgTimerOn) {
                SetTimer(hwnd, TID_MDIMG, 120, NULL);
                V.imgTimerOn = TRUE;
            }
            return 0;
        case WM_TIMER:
            if (wp == TID_MDIMG) {
                KillTimer(hwnd, TID_MDIMG);
                V.imgTimerOn = FALSE;
                if (V.docIdx >= 0 && V.docIdx < g_docCount) {
                    int save = V.scrollY;
                    LoadContentEx(V.docIdx, TRUE);
                    V.scrollY = save;
                    ClampScroll();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        case WM_SETFOCUS:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MdView_Register(void) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MdViewProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ETONMDView";
    RegisterClassExW(&wc);
    V.hHand = LoadCursorW(NULL, IDC_HAND);
}

void MdView_Create(HWND parent) {
    GdipInit();
    V.hwnd = CreateWindowExW(0, L"ETONMDView", NULL,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 0, 0, parent, NULL, g_hInst, NULL);
    MdImg_Init(V.hwnd);
    ShowWindow(V.hwnd, SW_HIDE);
    V.docIdx = -1;
}

BOOL MdView_IsVisible(void) {
    return V.hwnd && IsWindowVisible(V.hwnd);
}

static void LoadContentEx(int index, BOOL force) {
    if (!force && index >= 0 && index < g_docCount && V.docIdx == index &&
        V.root && V.modGen == g_docs[index].modGen)
        return;
    s_imgGen++;
    if (V.root) { BlkFree(V.root); V.root = NULL; }
    ItemResetAll();
    V.scrollY = 0;
    V.docIdx = index;
    V.modGen = (index >= 0 && index < g_docCount) ? g_docs[index].modGen : 0;

    V.baseDir[0] = L'\0';
    if (!g_docs[index].isNew && g_docs[index].path[0]) {
        wcscpy_s(V.baseDir, MAX_PATH, g_docs[index].path);
        wchar_t* slash = wcsrchr(V.baseDir, L'\\');
        if (slash) *slash = L'\0';
        else V.baseDir[0] = L'\0';
    }

    DWORD len = 0;
    char* utf8 = Editor_GetTextUtf8(index, &len);
    if (utf8) {
        V.root = MdParseDoc(utf8, len);
        free(utf8);
    }
    LayoutAll();
    ImgSweep();
}

static void LoadContent(int index) {
    LoadContentEx(index, FALSE);
}

/* ---- docx 导出所需的模型访问 ---- */

const MdBlock* MdView_EnsureDocModel(void) {
    if (!V.hwnd || g_curDoc < 0 || g_curDoc >= g_docCount) return NULL;
    int saveScroll = V.scrollY;
    LoadContent(g_curDoc);          /* 幂等：内容未变时直接复用 */
    V.scrollY = saveScroll;
    return V.root;
}

const MdFonts* MdView_DocFonts(HDC hdc) {
    FontsEnsure(hdc);
    return V.fontsOk ? &V.fonts : NULL;
}

BOOL MdView_ResolveImage(const wchar_t* href, wchar_t* out, int cch) {
    out[0] = L'\0';
    if (!href || !*href) return FALSE;
    if (_wcsnicmp(href, L"http://", 7) == 0 || _wcsnicmp(href, L"https://", 8) == 0)
        return MdImg_CachePath(href, out, cch) &&
               GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
    return ResolveLocalHref(href, out, cch);
}

BOOL MdView_ImageSize(const wchar_t* full, UINT* w, UINT* h) {
    ImgEnt* e = ImgGet(full);
    if (!e) return FALSE;
    *w = e->w;
    *h = e->h;
    return TRUE;
}

void MdView_OnActivate(void) {
    if (!V.hwnd) return;
    BOOL isMd = (g_curDoc >= 0 && g_curDoc < g_docCount &&
                 g_docs[g_curDoc].lang == LANG_MD);
    BOOL show = isMd && (g_docs[g_curDoc].previewOn || g_mdSplit);
    HWND hed = (g_curDoc >= 0 && g_curDoc < g_docCount) ? g_docs[g_curDoc].hwndEdit : NULL;
    if (show) {
        LoadContent(g_curDoc);
        if (g_mdSplit) {
            ShowWindow(V.hwnd, SW_SHOW);
            if (hed) ShowWindow(hed, SW_SHOW);
        } else {
            ShowWindow(V.hwnd, SW_SHOW);
            SetFocus(V.hwnd);
        }
    } else {
        if (hed) {
            ShowWindow(hed, SW_SHOW);
            SetFocus(hed);
        }
        ShowWindow(V.hwnd, SW_HIDE);
    }
    int mode = show ? (g_mdSplit ? 2 : 1) : 0;
    static int lastMode = 0;
    if (mode != lastMode) {
        lastMode = mode;
        Editor_Layout();
    }
    if (show && !g_mdSplit && hed) ShowWindow(hed, SW_HIDE);
    if (show) InvalidateRect(V.hwnd, NULL, FALSE);
}

void MdView_Toggle(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    Doc* d = &g_docs[g_curDoc];
    if (d->lang != LANG_MD) return;
    d->previewOn = !d->previewOn;
    if (d->previewOn) g_mdSplit = FALSE;
    MdView_OnActivate();
}

void MdView_ToggleSplit(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    if (g_docs[g_curDoc].lang != LANG_MD) return;
    g_mdSplit = !g_mdSplit;
    if (g_mdSplit) g_docs[g_curDoc].previewOn = FALSE;
    MdView_OnActivate();
}

double MdView_GetScrollFraction(void) {
    int maxScroll = V.docH - V.clientH;
    if (maxScroll <= 0) return 0.0;
    return (double)V.scrollY / maxScroll;
}

void MdView_SyncScrollFromEdit(double frac) {
    if (!V.hwnd || !MdView_IsVisible()) return;
    int maxScroll = V.docH - V.clientH;
    if (maxScroll <= 0) return;
    int y = (int)(frac * maxScroll + 0.5);
    if (y < 0) y = 0;
    if (y > maxScroll) y = maxScroll;
    if (y != V.scrollY) {
        V.scrollY = y;
        InvalidateRect(V.hwnd, NULL, FALSE);
    }
}

void MdView_OnLayout(int x, int y, int w, int h) {
    if (!V.hwnd) return;
    SetWindowPos(V.hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MdView_OnDpiChanged(void) {
    FontsFree();
    LayoutAll();
}

void MdView_OnThemeChange(void) {
    InvalidateRect(V.hwnd, NULL, FALSE);
}

void MdView_OnZoom(void) {
    if (!MdView_IsVisible()) return;
    FontsFree();
    LayoutAll();
}

void MdView_RefreshIfActive(int index) {
    if (MdView_IsVisible() && index == g_curDoc && V.docIdx == index)
        LoadContent(index);
}

void MdView_Reset(void) {
    V.docIdx = -1;
}

BOOL MdView_PrintPages(HDC hdc, int pw, int ph) {
    if (!V.hwnd || pw <= 0 || ph <= 0) return FALSE;
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return FALSE;

    int targetW = MulDiv(760, (int)g_dpi, 96);
    int h = V.clientH > 0 ? V.clientH : 800;
    SetWindowPos(V.hwnd, NULL, 0, 0, targetW, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    if (V.docIdx != g_curDoc || !V.root || V.nItems == 0)
        LoadContent(g_curDoc);
    LayoutEnsure(0x7FFFFFFF);   /* 打印需要精确的 docH 与完整 items */
    if (V.nItems == 0 || V.docH <= 0) return FALSE;

    double scale = (double)pw / (double)targetW;
    int pageH = (int)(ph / scale);
    int saveScroll = V.scrollY, saveClientH = V.clientH;
    int pages = (V.docH + pageH - 1) / pageH;
    if (pages > 500) pages = 500;

    MdTheme th;
    MdTheme_Build(&th);
    SetGraphicsMode(hdc, GM_ADVANCED);
    for (int p = 0; p < pages; p++) {
        if (StartPage(hdc) <= 0) break;
        XFORM xf = { (float)scale, 0.0f, 0.0f, (float)scale, 0.0f, 0.0f };
        SetWorldTransform(hdc, &xf);
        V.scrollY = p * pageH;
        V.clientH = pageH + 8;
        PaintContent(hdc, &th, FALSE);
        XFORM id = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
        SetWorldTransform(hdc, &id);
        EndPage(hdc);
    }
    V.scrollY = saveScroll;
    V.clientH = saveClientH;
    return TRUE;
}
