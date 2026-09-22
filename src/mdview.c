#include "common.h"
#include "md4c.h"

enum { STY_BOLD = 1, STY_EM = 2, STY_CODE = 4, STY_STRIKE = 8,
       STY_LINK = 16, STY_IMG = 32, STY_BR = 64,
       STY_MATH = 128, STY_MATHDISP = 256 };

typedef struct MdRun {
    wchar_t* text;
    unsigned style;
    wchar_t* href;      /* 链接目标；图片运行为图片源 */
    wchar_t* link;      /* 包裹图片的链接目标（<a><img/></a>），点击用 */
    int imgW;           /* HTML width 属性（96dpi 像素），0 = 自然尺寸 */
    MathBox* math;
} MdRun;

typedef struct MdBlock MdBlock;

typedef enum {
    MDB_P, MDB_H, MDB_CODE, MDB_HTML, MDB_HR,
    MDB_QUOTE, MDB_UL, MDB_OL, MDB_LI, MDB_TABLE
} MdBlockType;

struct MdBlock {
    int type;
    MdRun* runs; int nRuns, capRuns;
    int level;
    wchar_t* code; int codeLen;
    char fenceLang[24];
    MermaidDiagram* diag;
    BOOL isTask; wchar_t taskMark;
    int itemNum; wchar_t itemDelim;
    BOOL tight;
    int nCols, nRows;
    int* aligns;
    MdBlock** cells;
    MdBlock** children; int nChildren, capChildren;
};

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
    MdRun* mathRun;
    MdRun* ownRun;
    wchar_t* href;      /* 图片项的点击目标（来自包裹链接或图片源） */
} MdItem;

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
} V;

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
    if (!p_GdipStartup || !p_GdipCreateBitmapFromFile) return;
    GdipStartupInput si;
    ZeroMemory(&si, sizeof(si));
    si.GdiplusVersion = 1;
    ULONG_PTR tok = 0;
    if (p_GdipStartup(&tok, &si, NULL) == 0) s_gdipOk = TRUE;
}

typedef struct { wchar_t path[MAX_PATH]; GpImage* img; UINT w, h; unsigned gen; } ImgEnt;
static ImgEnt s_imgs[16];
static int s_nImgs = 0;
static unsigned s_imgGen = 0;

static ImgEnt* ImgFind(const wchar_t* full) {
    for (int i = 0; i < s_nImgs; i++)
        if (_wcsicmp(s_imgs[i].path, full) == 0) return &s_imgs[i];
    return NULL;
}

static ImgEnt* ImgGet(const wchar_t* full) {
    if (!s_gdipOk || !full || !full[0]) return NULL;
    ImgEnt* hit = ImgFind(full);
    if (hit) { hit->gen = s_imgGen; return hit->img ? hit : NULL; }
    GpImage* im = NULL;
    if (p_GdipCreateBitmapFromFile(full, &im) != 0 || !im) return NULL;
    UINT w = 0, h = 0;
    p_GdipGetImageWidth(im, &w);
    p_GdipGetImageHeight(im, &h);
    if (!w || !h) { p_GdipDisposeImage(im); return NULL; }
    /* 满时淘汰最久未使用（gen 最小）的槽位，而不是固定挤掉 0 号 */
    int slot = 0;
    if (s_nImgs < 16) slot = s_nImgs++;
    else {
        for (int i = 1; i < 16; i++) if (s_imgs[i].gen < s_imgs[slot].gen) slot = i;
        if (s_imgs[slot].img) p_GdipDisposeImage(s_imgs[slot].img);
    }
    wcscpy_s(s_imgs[slot].path, MAX_PATH, full);
    s_imgs[slot].img = im;
    s_imgs[slot].w = w;
    s_imgs[slot].h = h;
    s_imgs[slot].gen = s_imgGen;
    return &s_imgs[slot];
}

/* 释放本次布局未引用的位图（换文档/内容变更后） */
static void ImgSweep(void) {
    int keep = 0;
    for (int i = 0; i < s_nImgs; i++) {
        if (s_imgs[i].gen == s_imgGen) s_imgs[keep++] = s_imgs[i];
        else if (s_imgs[i].img) p_GdipDisposeImage(s_imgs[i].img);
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

static void BlkFree(MdBlock* b) {
    if (!b) return;
    for (int i = 0; i < b->nRuns; i++) {
        free(b->runs[i].text);
        free(b->runs[i].href);
        free(b->runs[i].link);
        if (b->runs[i].math) Math_Free(b->runs[i].math);
    }
    free(b->runs);
    free(b->code);
    if (b->diag) Mermaid_Free(b->diag);
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
            if (rem >= 2 + n && strncmp(s + 1, kNamed[i].name, n) == 0) {
                out[0] = kNamed[i].ch;
                *outLen = 1;
                return 2 + n;
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
                c->diag = Mermaid_Parse(P->codeBuf, P->codeLen);
                if (c->diag) {
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
    MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS,
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
    for (int i = 0; i < V.nItems; i++) {
        if (V.items[i].type == ITM_LINES) {
            for (int l = 0; l < V.items[i].nLines; l++)
                free(V.items[i].lines[l].frags);
            free(V.items[i].lines);
        }
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
        HFONT of = (HFONT)SelectObject(hdc, bf);
        SIZE sz; GetTextExtentPoint32W(hdc, L" ", 1, &sz);
        spaceW = sz.cx;
        SelectObject(hdc, of);
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
                if (!mr2->math) mr2->math = Math_Build(mr2->text);
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
                    GetTextMetricsW(hdc, &curTm);
                    curFont = fnt;
                }
                SIZE sz;
                GetTextExtentPoint32W(hdc, tk->s, tk->len, &sz);
                if (nF > 0 && x + sz.cx > avail) { flush = TRUE; overflow = TRUE; }
                else {
                    if (curTm.tmHeight > lineH) lineH = curTm.tmHeight;
                    if (curTm.tmAscent > asc) asc = curTm.tmAscent;
                    MD_GROW(fr, nF, capF, MdFrag);
                    fr[nF].run = tk->run; fr[nF].s = tk->s; fr[nF].len = tk->len;
                    fr[nF].x = x; fr[nF].w = sz.cx;
                    nF++;
                    x += sz.cx;
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
    MdLine* lines = NULL; int nL = 0, capL = 0;
    MdRun* codeRun = (MdRun*)calloc(1, sizeof(MdRun));
    codeRun->style = 0; codeRun->href = NULL; codeRun->text = NULL;
    wchar_t* p = b->code;
    while (p && *p) {
        wchar_t* nl = wcschr(p, L'\n');
        int len = nl ? (int)(nl - p) : (int)wcslen(p);
        if (len > 0 && p[len-1] == L'\r') len--;
        wchar_t saved = p[len];
        p[len] = L'\0';
        codeRun->text = p;
        MdLine* sub; int subN;
        WrapRuns(hdc, codeRun, 1, avail - UI_Scale(20), ROLE_MONO, &sub, &subN);
        p[len] = saved;
        MD_GROW(lines, nL + subN, capL, MdLine);
        for (int i = 0; i < subN; i++) lines[nL++] = sub[i];
        free(sub);
        if (!nl) break;
        p = nl + 1;
    }
    codeRun->text = NULL;
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
    it->ownRun = codeRun;
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
    if (!mr->math) mr->math = Math_Build(mr->text);
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

static void LayoutAll(void) {
    if (V.clientW <= 0 || V.clientH <= 0) {
        V.lastLayoutW = -1;
        return;
    }
    ItemResetAll();

    HDC hdc = GetDC(V.hwnd);
    FontsEnsure(hdc);
    int x0 = UI_Scale(28);
    int avail = V.clientW - UI_Scale(28) * 2 - UI_Scale(12);
    if (avail < UI_Scale(100)) avail = UI_Scale(100);
    int y = UI_Scale(16);
    if (V.root)
        for (int i = 0; i < V.root->nChildren; i++)
            LayoutBlock(hdc, V.root->children[i], x0, avail, &y, 0);
    V.docH = y + UI_Scale(40);
    V.lastLayoutW = V.clientW;
    ReleaseDC(V.hwnd, hdc);
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

static void PaintContent(HDC hdc, const MdTheme* th) {
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
                GpGraphics* g = NULL;
                if (p_GdipCreateFromHDC(hdc, &g) == 0 && g) {
                    p_GdipSetInterpolationMode(g, 7 );
                    RECT rc = it->rc;
                    OffsetRect(&rc, 0, -V.scrollY);
                    p_GdipDrawImageRectRectI(g, (GpImage*)it->image,
                        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                        0, 0, (int)it->imageW, (int)it->imageH,
                        2 , NULL, NULL, NULL);
                    p_GdipFlush(g, 1 );
                    p_GdipDeleteGraphics(g);
                }
                break;
            }
            case ITM_MATH: {
                if (it->mathRun && it->mathRun->math) {
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
                            SetTextColor(hdc, th->codeFg);
                        } else {
                            SetTextColor(hdc, th->fg);
                        }
                        SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
                        TextOutW(hdc, fx, baseY, fr->s, fr->len);
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
    PaintContent(mem, &th);
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

static void LoadContentEx(int index, BOOL force);

static LRESULT CALLBACK MdViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
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
            const wchar_t* href = HitLink(px, py);
            if (href) OpenLink(href);
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
                return 0;
            }
            const wchar_t* href = HitLink(px, py);
            SetCursor(href ? V.hHand : LoadCursorW(NULL, IDC_ARROW));
            return 0;
        }
        case WM_LBUTTONUP:
            if (V.sbDrag) { V.sbDrag = FALSE; ReleaseCapture(); }
            return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_ESCAPE:
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
                case VK_END:   ScrollBy(V.docH); return 0;
            }
            break;
        case WM_MDIMG_READY:
            if (V.docIdx >= 0 && V.docIdx < g_docCount) {
                int save = V.scrollY;
                LoadContentEx(V.docIdx, TRUE);
                V.scrollY = save;
                ClampScroll();
                InvalidateRect(hwnd, NULL, FALSE);
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
        PaintContent(hdc, &th);
        XFORM id = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
        SetWorldTransform(hdc, &id);
        EndPage(hdc);
    }
    V.scrollY = saveScroll;
    V.clientH = saveClientH;
    return TRUE;
}
