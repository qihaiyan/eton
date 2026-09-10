/* mdview.c — Markdown 原生预览视图（MD4C 解析 + GDI 自绘）
   参照 tinta 的架构：解析（md4c.c）→ 块树 → 按宽度排版 → 双缓冲绘制，
   零 Web 引擎。单一全局子窗口 "ETONMDView"，按当前文档 previewOn 显隐。
   交互：F12/菜单切换、Esc 退出、滚轮/键盘滚动、链接点击（相对路径按
   文档所在目录解析）、悬停手型。Mermaid 代码块走 mermaid.c 原生渲染，
   不支持的图族回退为代码块显示。 */

#include "common.h"
#include "md4c.h"

/* ============================ 模型 ============================ */

/* 行内样式位 */
enum { STY_BOLD = 1, STY_EM = 2, STY_CODE = 4, STY_STRIKE = 8,
       STY_LINK = 16, STY_IMG = 32, STY_BR = 64,
       STY_MATH = 128, STY_MATHDISP = 256 };

typedef struct MdRun {
    wchar_t* text;        /* 拥有；STY_BR 时为 NULL */
    unsigned style;
    wchar_t* href;        /* 拥有；链接/图片目标 */
    MathBox* math;        /* STY_MATH：由 text(LaTeX) 构建的排版盒（拥有） */
} MdRun;

typedef struct MdBlock MdBlock;

typedef enum {
    MDB_P, MDB_H, MDB_CODE, MDB_HTML, MDB_HR,
    MDB_QUOTE, MDB_UL, MDB_OL, MDB_LI, MDB_TABLE
} MdBlockType;

struct MdBlock {
    int type;
    MdRun* runs; int nRuns;             /* 段落/标题/单元格文本 */
    int level;                          /* 标题级别 1..6 */
    wchar_t* code; int codeLen;         /* 代码块原文（宽字符） */
    char fenceLang[24];
    MermaidDiagram* diag;               /* mermaid 已解析图表（拥有） */
    BOOL isTask; wchar_t taskMark;      /* 任务列表项 */
    int itemNum; wchar_t itemDelim;     /* 有序编号 + 分隔符 */
    BOOL tight;                         /* 紧凑列表 */
    int nCols, nRows;
    int* aligns;                        /* 每列对齐 0左 1中 2右 */
    MdBlock** cells;                    /* nRows*nCols 个单元格块（拥有） */
    MdBlock** children; int nChildren;
};

/* 排版产物（绘制原语，文档坐标系） */
typedef enum { ITM_LINES, ITM_RECT, ITM_FRAME, ITM_DIAGRAM, ITM_IMAGE, ITM_MATH } MdItemType;
/* ITM_RECT 子类型 */
enum { RCT_CODEBG = 0, RCT_QUOTEBAR, RCT_HRULE, RCT_TABLEHEAD, RCT_CANVAS };

typedef struct MdFrag {
    MdRun* run;
    const wchar_t* s; int len;          /* 指向 run->text 内部，零拷贝 */
    int x, w;
} MdFrag;

typedef struct MdLine {
    int y, h, asc;                      /* asc = 基线相对行顶偏移 */
    MdFrag* frags; int nF;
} MdLine;

typedef struct MdItem {
    int type;
    RECT rc;
    int flag;
    MdLine* lines; int nLines;
    int role;                           /* 字体角色：0=body 1..6=标题 7=mono 8=small */
    MermaidDiagram* diag;
    void* image;                        /* ITM_IMAGE：GpImage*（缓存所有，勿随 item 释放） */
    UINT imageW, imageH;
    MdRun* mathRun;                     /* ITM_MATH：借用（盒随 run 释放） */
    MdRun* ownRun;                      /* 合成 run（列表标记等），随 item 释放 */
} MdItem;

/* ============================ 视图状态 ============================ */

static struct {
    HWND hwnd;
    MdBlock* root;                      /* 当前文档模型 */
    int docIdx;                         /* 模型对应 g_docs 下标，-1 无 */
    MdItem* items; int nItems, capItems;
    int docH, scrollY, clientW, clientH;
    MdFonts fonts;
    BOOL fontsOk;
    wchar_t baseDir[MAX_PATH];          /* 文档目录（相对链接解析） */
    HCURSOR hHand;
    /* 自绘滚动条 */
    BOOL sbDrag; int sbDragOff;
    int lastLayoutW;
} V;

/* ============================ GDI+ 图片（平铺 API，动态加载） ============================ */
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
    /* 注意：Startup/Shutdown 导出名是 "Gdiplus" 前缀，其余 flat API 是 "Gdip" */
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

/* 图片缓存（路径 → GpImage*，LRU 上限 16） */
typedef struct { wchar_t path[MAX_PATH]; GpImage* img; UINT w, h; } ImgEnt;
static ImgEnt s_imgs[16];
static int s_nImgs = 0;

static ImgEnt* ImgFind(const wchar_t* full) {
    for (int i = 0; i < s_nImgs; i++)
        if (_wcsicmp(s_imgs[i].path, full) == 0) return &s_imgs[i];
    return NULL;
}

static ImgEnt* ImgGet(const wchar_t* full) {
    if (!s_gdipOk || !full || !full[0]) return NULL;
    ImgEnt* hit = ImgFind(full);
    if (hit) return hit->img ? hit : NULL;
    GpImage* im = NULL;
    if (p_GdipCreateBitmapFromFile(full, &im) != 0 || !im) return NULL;
    UINT w = 0, h = 0;
    p_GdipGetImageWidth(im, &w);
    p_GdipGetImageHeight(im, &h);
    if (!w || !h) { p_GdipDisposeImage(im); return NULL; }
    int slot = s_nImgs < 16 ? s_nImgs++ : 0;   /* 满了覆盖 0 号（简单策略） */
    if (s_imgs[slot].img) p_GdipDisposeImage(s_imgs[slot].img);
    wcscpy_s(s_imgs[slot].path, MAX_PATH, full);
    s_imgs[slot].img = im;
    s_imgs[slot].w = w;
    s_imgs[slot].h = h;
    return &s_imgs[slot];
}

/* ============================ 小工具 ============================ */

static MdBlock* BlkNew(int type) {
    MdBlock* b = (MdBlock*)calloc(1, sizeof(MdBlock));
    if (b) b->type = type;
    return b;
}

static void BlkAppend(MdBlock* parent, MdBlock* child) {
    MdBlock** nc = (MdBlock**)realloc(parent->children,
        ((size_t)parent->nChildren + 1) * sizeof(MdBlock*));
    if (!nc) return;
    parent->children = nc;
    parent->children[parent->nChildren++] = child;
}

static void BlkFree(MdBlock* b) {
    if (!b) return;
    for (int i = 0; i < b->nRuns; i++) {
        free(b->runs[i].text);
        free(b->runs[i].href);
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

/* UTF-8 片段 → 新分配宽字符串 */
static wchar_t* Utf8ToW(const char* s, int len) {
    if (len <= 0) len = (int)strlen(s);
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    wchar_t* w = (wchar_t*)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
    w[n] = L'\0';
    return w;
}

/* ============================ MD4C 解析 ============================ */

typedef struct {
    MdBlock* stack[64]; int depth;      /* 容器栈 */
    MdBlock* textBlk;                   /* 当前收集文本的块（P/H/TH/TD） */
    unsigned style;
    wchar_t* href;                      /* 活动链接目标（链接不嵌套） */
    char* codeBuf; int codeLen, codeCap;   /* 代码/HTML 块 UTF-8 累积 */
    char* mathBuf; int mathLen;            /* 数学 span 的 LaTeX 原文累积 */
    int inMath; BOOL mathDisp;
    MdBlock* codeBlk;                   /* 当前代码块 */
    int quoteDepth;
    int olStart[64]; char olDelim[64]; int olNum[64]; BOOL inOl[64];
    MdBlock* table;                     /* 当前表格 */
    int cellCount;                      /* 已收集单元格数 */
} MdParseCtx;

static MdParseCtx* P;                   /* 回调用户数据（单线程同步解析） */

static void AddRun(const wchar_t* s, int len, unsigned style) {
    if (!P->textBlk) {
        /* 紧凑列表（条目间无空行）不发出 MD_BLOCK_P，内联文本直接挂在 LI 下：
           惰性建段落并入树，否则整段文字会被静默丢弃 */
        MdBlock* top = P->stack[P->depth];
        if (top && top->type == MDB_LI) {
            P->textBlk = BlkNew(MDB_P);
            BlkAppend(top, P->textBlk);
        } else {
            return;
        }
    }
    MdRun* nr = (MdRun*)realloc(P->textBlk->runs,
        ((size_t)P->textBlk->nRuns + 1) * sizeof(MdRun));
    if (!nr) return;
    P->textBlk->runs = nr;
    MdRun* r = &nr[P->textBlk->nRuns++];
    r->style = style;
    r->math = NULL;
    r->text = (wchar_t*)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (r->text) {
        memcpy(r->text, s, (size_t)len * sizeof(wchar_t));
        r->text[len] = L'\0';
    }
    r->href = (style & (STY_LINK | STY_IMG)) && P->href ? _wcsdup(P->href) : NULL;
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

/* MD_ATTRIBUTE（lang/href 等）→ 宽字符串（整体取 text；实体引用罕见，按原文处理） */
static void AttrToWide(MD_ATTRIBUTE a, wchar_t* out, int cch) {
    int n = MultiByteToWideChar(CP_UTF8, 0, a.text, (int)a.size, out, cch - 1);
    if (n < 0) n = 0;
    out[n] = L'\0';
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
            /* 列对齐来自单元格 detail（以表头行为准写入列属性） */
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
            /* 紧凑列表的惰性段落已在 AddRun 时挂入 LI，这里只需复位 */
            P->textBlk = NULL;
            if (P->depth > 0) P->depth--;
            if (type == MD_BLOCK_TABLE) P->table = NULL;
            break;
        case MD_BLOCK_H: case MD_BLOCK_P:
            if (P->textBlk) BlkAppend(P->stack[P->depth], P->textBlk);
            P->textBlk = NULL;
            break;
        case MD_BLOCK_TH: case MD_BLOCK_TD: {
            MdBlock* cell = P->textBlk;
            P->textBlk = NULL;
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
                /* 去掉结尾多余空行 */
                while (P->codeLen > 0 && (P->codeBuf[P->codeLen-1] == '\n' ||
                       P->codeBuf[P->codeLen-1] == '\r')) P->codeLen--;
            }
            if (type == MD_BLOCK_CODE && P->codeLen > 0 &&
                _stricmp(c->fenceLang, "mermaid") == 0) {
                c->diag = Mermaid_Parse(P->codeBuf, P->codeLen);
                if (c->diag) {
                    BlkAppend(P->stack[P->depth], c);
                    break;
                }
                /* 解析失败 → 回退为代码块，首行给提示 */
                wchar_t hint[256];
                wsprintf(hint, L"[%s]", T(STR_MD_MERMAID_UNSUP));
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
            P->style |= STY_LINK;
            const MD_SPAN_A_DETAIL* d = (const MD_SPAN_A_DETAIL*)detail;
            wchar_t tmp[2048];
            AttrToWide(d->href, tmp, 2048);
            P->href = _wcsdup(tmp);
            break;
        }
        case MD_SPAN_IMG: {
            P->style |= STY_IMG;
            const MD_SPAN_IMG_DETAIL* d = (const MD_SPAN_IMG_DETAIL*)detail;
            wchar_t tmp[2048];
            AttrToWide(d->src, tmp, 2048);
            P->href = _wcsdup(tmp);
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
            P->style &= ~STY_LINK;
            free(P->href); P->href = NULL;
            break;
        case MD_SPAN_IMG:
            P->style &= ~STY_IMG;
            free(P->href); P->href = NULL;
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
            else {
                wchar_t* w = Utf8ToW(text, (int)len);
                if (w) { AddRun(w, (int)wcslen(w), P->style | STY_CODE); free(w); }
            }
            break;
        }
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            AddRun(L"", 0, STY_BR | P->style);   /* 排版时强制换行 */
            break;
        case MD_TEXT_LATEXMATH: {
            /* 数学原文累积（在 enter/leave span 之间整段收齐） */
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
    MdBlock* root = BlkNew(MDB_QUOTE);      /* 复用容器类型作为根（type 不用于绘制） */
    root->type = MDB_P;                      /* 根类型无意义 */
    MdParseCtx ctx;
    ZeroMemory(&ctx, sizeof(ctx));
    ctx.stack[0] = root;
    P = &ctx;
    md_parse(utf8, (MD_SIZE)len, &g_mdParser, &ctx);
    free(ctx.href);
    free(ctx.mathBuf);
    free(ctx.codeBuf);
    return root;
}

/* ============================ 字体 ============================ */

static HFONT MkFont(int pt, int weight, BOOL italic, const wchar_t* face) {
    int h = -MulDiv(pt, (int)g_dpi, 72);
    if (h > -4) h = -4;
    return CreateFontW(h, 0, 0, 0, weight, italic, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void FontsFree(void) {
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
    /* 数学字体（Cambria Math）：正文 / 脚本(0.72) / 脚本的脚本(0.55) × 正/斜体 */
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

/* 字体角色 */
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

/* ============================ 排版 ============================ */

static void ItemResetAll(void) {
    for (int i = 0; i < V.nItems; i++) {
        if (V.items[i].type == ITM_LINES) {
            for (int l = 0; l < V.items[i].nLines; l++)
                free(V.items[i].lines[l].frags);
            free(V.items[i].lines);
        }
        if (V.items[i].ownRun) {
            free(V.items[i].ownRun->text);
            free(V.items[i].ownRun);
        }
    }
    free(V.items);
    V.items = NULL; V.nItems = 0; V.capItems = 0;
}

static void ItemPush(int type, RECT rc, int flag) {
    V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
    MdItem* it = &V.items[V.nItems++];
    ZeroMemory(it, sizeof(*it));
    it->type = type; it->rc = rc; it->flag = flag;
    it->role = ROLE_BODY;
}

/* 词元：把 run 文本切成可断行的原子（ASCII 词 / CJK 单字 / 空格） */
typedef struct Tok { MdRun* run; const wchar_t* s; int len; BOOL space; } Tok;

static int WrapRuns(HDC hdc, MdRun* runs, int nRuns, int avail, int role,
                    MdLine** outLines, int* outN) {
    Tok* toks = NULL; int nT = 0, capT = 0;
    for (int i = 0; i < nRuns; i++) {
        MdRun* r = &runs[i];
        if (r->style & STY_BR) {   /* 强制换行标记 */
            toks = (Tok*)realloc(toks, ((size_t)nT + 1) * sizeof(Tok));
            toks[nT].run = r; toks[nT].s = NULL; toks[nT].len = 0; toks[nT].space = FALSE;
            nT++; continue;
        }
        if (r->style & STY_MATH) {  /* 数学：整个 run 是一个原子单元 */
            toks = (Tok*)realloc(toks, ((size_t)nT + 1) * sizeof(Tok));
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
                toks = (Tok*)realloc(toks, ((size_t)nT + 1) * sizeof(Tok));
                toks[nT].run = r; toks[nT].s = t + start; toks[nT].len = i2 - start; toks[nT].space = TRUE;
                nT++;
            } else if (IsCJK(t[i2])) {
                i2++;
                toks = (Tok*)realloc(toks, ((size_t)nT + 1) * sizeof(Tok));
                toks[nT].run = r; toks[nT].s = t + start; toks[nT].len = 1; toks[nT].space = FALSE;
                nT++;
            } else {
                while (i2 < L && t[i2] != L' ' && t[i2] != L'\t' && !IsCJK(t[i2])) i2++;
                toks = (Tok*)realloc(toks, ((size_t)nT + 1) * sizeof(Tok));
                toks[nT].run = r; toks[nT].s = t + start; toks[nT].len = i2 - start; toks[nT].space = FALSE;
                nT++;
            }
        }
    }

    MdLine* lines = NULL; int nL = 0;
    MdFrag* fr = NULL; int nF = 0;
    int x = 0, lineH = 0, asc = 0;
    HFONT curFont = NULL;

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
                    fr = (MdFrag*)realloc(fr, ((size_t)nF + 1) * sizeof(MdFrag));
                    fr[nF].run = tk->run; fr[nF].s = tk->s; fr[nF].len = tk->len;
                    fr[nF].x = x; fr[nF].w = 0;
                    nF++;
                    x += spaceW;
                }
                continue;
            }
            if (tk->run->style & STY_MATH) {   /* 数学原子单元 */
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
                    fr = (MdFrag*)realloc(fr, ((size_t)nF + 1) * sizeof(MdFrag));
                    fr[nF].run = mr2; fr[nF].s = mr2->text; fr[nF].len = 0;
                    fr[nF].x = x; fr[nF].w = mw;
                    nF++;
                    x += mw;
                    continue;
                }
            } else {
                HFONT fnt = FontFor(tk->run->style, role);
                if (fnt != curFont) { SelectObject(hdc, fnt); curFont = fnt; }
                SIZE sz;
                GetTextExtentPoint32W(hdc, tk->s, tk->len, &sz);
                if (nF > 0 && x + sz.cx > avail) { flush = TRUE; overflow = TRUE; }
                else {
                    TEXTMETRICW tm;
                    GetTextMetricsW(hdc, &tm);
                    if (tm.tmHeight > lineH) lineH = tm.tmHeight;
                    if (tm.tmAscent > asc) asc = tm.tmAscent;
                    fr = (MdFrag*)realloc(fr, ((size_t)nF + 1) * sizeof(MdFrag));
                    fr[nF].run = tk->run; fr[nF].s = tk->s; fr[nF].len = tk->len;
                    fr[nF].x = x; fr[nF].w = sz.cx;
                    nF++;
                    x += sz.cx;
                    continue;
                }
            }
        }
        /* 落行 */
        if (nF > 0) {
            /* 去掉行尾空格 frag（宽度已含） */
            while (nF > 0 && fr[nF-1].run->text && fr[nF-1].len > 0 &&
                   (fr[nF-1].s[0] == L' ')) {
                nF--;
            }
            lines = (MdLine*)realloc(lines, ((size_t)nL + 1) * sizeof(MdLine));
            MdLine* ln = &lines[nL++];
            ln->frags = fr; ln->nF = nF;
            ln->h = lineH ? lineH : V.fonts.lineH;
            ln->asc = asc;
            ln->y = 0;
            fr = NULL; nF = 0;
        }
        x = 0; lineH = 0; asc = 0;
        if (overflow) i--;   /* 超宽落行：当前 token 下一轮重放，否则该词会被整个丢掉 */
    }
    free(toks);
    if (curFont) SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    *outLines = lines;
    *outN = nL;
    return nL;
}

/* 排版一个文本块（段落/标题），追加 ITM_LINES；返回高度 */
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
    V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
    MdItem* it = &V.items[V.nItems++];
    ZeroMemory(it, sizeof(*it));
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
        V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
        MdItem* it = &V.items[V.nItems++];
        ZeroMemory(it, sizeof(*it));
        it->type = ITM_DIAGRAM;
        it->rc.left = x0 + UI_Scale(8); it->rc.top = *y + UI_Scale(8);
        it->rc.right = it->rc.left + w; it->rc.bottom = it->rc.top + h;
        it->diag = b->diag;
        *y += h + UI_Scale(16) + UI_Scale(12);
        return;
    }
    /* 代码文本按行拆（合成 run 堆分配：frag 借用其指针到绘制阶段）。
       每行临时写入 NUL 截断再交给 WrapRuns——否则换行符会被当成普通
       词字符，整个后续文本并成一个超宽词元，代码块渲染成重叠乱码。 */
    MdLine* lines = NULL; int nL = 0;
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
        for (int i = 0; i < subN; i++) {
            lines = (MdLine*)realloc(lines, ((size_t)nL + 1) * sizeof(MdLine));
            lines[nL++] = sub[i];
        }
        free(sub);
        if (!nl) break;
        p = nl + 1;
    }
    codeRun->text = NULL;   /* text 指向块内缓冲，所有权在块，勿随 item 释放 */
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
    V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
    MdItem* it = &V.items[V.nItems++];
    ZeroMemory(it, sizeof(*it));
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
    /* 列自然宽（单行不折行估宽） */
    int* colW = (int*)calloc((size_t)nC, sizeof(int));
    int pad = UI_Scale(7);
    for (int r = 0; r < nR; r++) {
        for (int c = 0; c < nC; c++) {
            MdBlock* cell = b->cells[r * nC + c];
            for (int i = 0; i < cell->nRuns; i++) {
                HFONT f = FontFor(cell->runs[i].style, ROLE_BODY);
                int w = 0;
                HFONT of = (HFONT)SelectObject(hdc, f);
                SIZE sz;
                GetTextExtentPoint32W(hdc, cell->runs[i].text,
                                      (int)wcslen(cell->runs[i].text), &sz);
                SelectObject(hdc, of);
                w = sz.cx;
                if (w > colW[c]) colW[c] = w;
            }
        }
    }
    int total = pad * 2 * nC;
    for (int c = 0; c < nC; c++) total += colW[c];
    if (total > avail) {
        /* 等比收缩，保底列宽 */
        int natTotal = total;
        for (int c = 0; c < nC; c++) {
            colW[c] = colW[c] * (avail - pad * 2 * nC) / (natTotal - pad * 2 * nC);
            if (colW[c] < UI_Scale(30)) colW[c] = UI_Scale(30);
        }
        total = pad * 2 * nC;
        for (int c = 0; c < nC; c++) total += colW[c];
    }
    /* 每格排版 */
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
    /* 行高 */
    int* rowH = (int*)calloc((size_t)nR, sizeof(int));
    for (int r = 0; r < nR; r++) {
        int mx = V.fonts.lineH;
        for (int c = 0; c < nC; c++) {
            int need = lays[r * nC + c].h + pad * 2;
            if (need > mx) mx = need;
        }
        rowH[r] = mx;
    }
    /* 表头底色 */
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
                V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
                MdItem* it = &V.items[V.nItems++];
                ZeroMemory(it, sizeof(*it));
                it->type = ITM_LINES; it->rc = rc;
                it->lines = cl->lines; it->nLines = cl->nL;
                it->role = ROLE_BODY;
                /* 对齐：调整每行 frag 的 x */
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
            /* 列分隔线 */
            if (c + 1 < nC) {
                RECT vl = { xx + colW[c] + pad, yy, xx + colW[c] + pad + 1, yy + rowH[r] };
                ItemPush(ITM_RECT, vl, RCT_CODEBG);
            }
            xx += colW[c] + pad * 2;
        }
        /* 行分隔线（表头下） */
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
        /* 标记文本 */
        wchar_t mark[32];
        if (li->isTask)
            wcscpy(mark, (li->taskMark == L' ' || !li->taskMark) ? L"☐" : L"☑");
        else if (b->type == MDB_OL)
            wsprintf(mark, L"%d%c", li->itemNum, li->itemDelim ? li->itemDelim : L'.');
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
        /* 标记（合成 run 需堆分配：绘制发生在函数返回之后） */
        RECT mk = { x0, *y, x0 + mw, *y + V.fonts.lineH };
        V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
        {
            MdItem* it = &V.items[V.nItems++];
            ZeroMemory(it, sizeof(*it));
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
        /* 内容 */
        int inner = *y;
        for (int c = 0; c < li->nChildren; c++)
            LayoutBlock(hdc, li->children[c], x0 + indent, avail - indent, &inner, qd);
        /* 内容首行与标记对齐（若首块是文本，把标记画在首行基线；简化为同顶） */
        *y = inner > *y ? inner : *y + V.fonts.lineH;
        *y += itemGap;
        (void)startY;
    }
    *y += UI_Scale(2);
}

/* 相对路径解析为绝对路径（http/mailto/锚点返回 NULL） */
static wchar_t* ResolveLocalHref(const wchar_t* href) {
    if (!href || !*href || href[0] == L'#') return NULL;
    if (_wcsnicmp(href, L"http://", 7) == 0 || _wcsnicmp(href, L"https://", 8) == 0 ||
        _wcsnicmp(href, L"mailto:", 7) == 0) return NULL;
    static wchar_t full[MAX_PATH];
    if (PathIsRelativeW(href) && V.baseDir[0])
        PathCombineW(full, V.baseDir, href);
    else
        wcscpy_s(full, MAX_PATH, href);
    return full;
}

/* 独立图片段：段落里只有一个图片 run、其余全是空白 → 块级渲染图片 */
static BOOL LayoutImageParagraph(HDC hdc, MdBlock* b, int x0, int avail, int* y) {
    MdRun* img = NULL;
    for (int i = 0; i < b->nRuns; i++) {
        MdRun* r = &b->runs[i];
        if (r->style & STY_BR) continue;
        if (!r->text) continue;
        for (const wchar_t* p = r->text; *p; p++)
            if (*p != L' ' && *p != L'\t') {
                if (r->style & STY_IMG) {
                    if (img) return FALSE;   /* 两个图片混排 → 按文本渲染 */
                    img = r;
                    break;
                }
                return FALSE;                /* 有实际文字 → 按文本渲染 */
            }
    }
    if (!img) return FALSE;
    wchar_t* full = ResolveLocalHref(img->href ? img->href : L"");
    if (!full) return FALSE;                 /* 远程图片 → 仍按占位文本渲染 */
    ImgEnt* e = ImgGet(full);
    if (!e) return FALSE;                    /* 加载失败 → 占位文本 */

    /* 显示宽度：自然宽度按 DPI 折算，超过可用宽度则收缩 */
    double scale = g_dpi / 96.0;
    UINT w = (UINT)(e->w * scale + 0.5);
    UINT h = (UINT)(e->h * scale + 0.5);
    if (w > (UINT)avail) { h = (UINT)((double)h * avail / w); w = (UINT)avail; }
    if (!w || !h) return FALSE;

    V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
    MdItem* it = &V.items[V.nItems++];
    ZeroMemory(it, sizeof(*it));
    it->type = ITM_IMAGE;
    it->rc.left = x0; it->rc.top = *y;
    it->rc.right = x0 + (int)w; it->rc.bottom = *y + (int)h;
    it->image = e->img;
    it->imageW = e->w; it->imageH = e->h;
    *y += (int)h + UI_Scale(9);
    (void)hdc;
    return TRUE;
}

/* 块级公式：段落里只有一个 $$..$$ 公式 run、其余空白 → 居中绘制 */
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
        if (mr) return FALSE;   /* 多个公式混排 → 按内联处理 */
        mr = r;
    }
    if (!mr) return FALSE;
    if (!mr->math) mr->math = Math_Build(mr->text);
    if (!mr->math) return FALSE;
    Math_Measure(mr->math, hdc, &V.fonts);
    V.items = (MdItem*)realloc(V.items, ((size_t)V.nItems + 1) * sizeof(MdItem));
    MdItem* it = &V.items[V.nItems++];
    ZeroMemory(it, sizeof(*it));
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
        case MDB_LI:   /* LI 由 LayoutList 处理；不应直接到这里 */
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
        /* 窗口尚未获得尺寸（创建/首次显示前）：跳过本次，
           等 WM_SIZE 到来后再真正排版，避免按 0 宽排出废布局 */
        V.lastLayoutW = -1;
        return;
    }
    ItemResetAll();

    HDC hdc = GetDC(V.hwnd);
    FontsEnsure(hdc);
    int x0 = UI_Scale(28);
    int avail = V.clientW - UI_Scale(28) * 2 - UI_Scale(12);   /* 右侧留滚动条 */
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

/* ============================ 主题 ============================ */

void MdTheme_Build(MdTheme* th) {
    if (g_dark) {
        th->bg        = RGB(30,30,30);
        th->fg        = RGB(212,216,221);
        th->fgMuted   = RGB(139,148,158);
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

/* ============================ 绘制 ============================ */

static int SbThickness(void) { return UI_Scale(12); }

static void PaintContent(HDC hdc, const MdTheme* th) {
    RECT rcClient;
    GetClientRect(V.hwnd, &rcClient);
    int top = V.scrollY, bot = V.scrollY + V.clientH;

    HPEN ulPen = CreatePen(PS_SOLID, 1, th->link);
    HPEN strikePen = CreatePen(PS_SOLID, 1, th->fgMuted);
    HPEN framePen = CreatePen(PS_SOLID, 1, th->codeBorder);

    for (int i = 0; i < V.nItems; i++) {
        MdItem* it = &V.items[i];
        if (it->rc.top >= bot || it->rc.bottom <= top) continue;
        switch (it->type) {
            case ITM_RECT: {
                HBRUSH br = NULL;
                switch (it->flag) {
                    case RCT_CODEBG: case RCT_TABLEHEAD: br = CreateSolidBrush(th->codeBg); break;
                    case RCT_QUOTEBAR: br = CreateSolidBrush(th->quoteBar); break;
                    case RCT_HRULE: br = CreateSolidBrush(th->hrule); break;
                    case RCT_CANVAS: br = CreateSolidBrush(th->selBg); break;
                }
                if (br) {
                    RECT rc = it->rc;
                    OffsetRect(&rc, 0, -V.scrollY);
                    FillRect(hdc, &rc, br);
                    DeleteObject(br);
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
                    /* 裁剪到画布：图表元素绝不越界串到相邻块 */
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
                    p_GdipSetInterpolationMode(g, 7 /*HighQualityBicubic*/);
                    RECT rc = it->rc;
                    OffsetRect(&rc, 0, -V.scrollY);
                    p_GdipDrawImageRectRectI(g, (GpImage*)it->image,
                        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                        0, 0, (int)it->imageW, (int)it->imageH,
                        2 /*UnitPixel*/, NULL, NULL, NULL);
                    p_GdipFlush(g, 1 /*FlushIntentionFlush*/);
                    p_GdipDeleteGraphics(g);
                }
                break;
            }
            case ITM_MATH: {
                /* 块级公式：居中绘制 */
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
                        /* 数学原子单元：以排版盒绘制，不走文本路径 */
                        if ((run->style & STY_MATH) && run->math) {
                            int fx2 = it->rc.left + fr->x;
                            Math_Draw(run->math, hdc, fx2, baseY, th, &V.fonts);
                            continue;
                        }
                        /* 判空看 frag 的 s：代码块的合成 run->text 故意为 NULL
                           （文字在块缓冲里，frag 自带指针与长度才是绘制数据） */
                        if (!fr->s || fr->len == 0) continue;
                        int fx = it->rc.left + fr->x;
                        HFONT f = FontFor(run->style, it->role);
                        HFONT of = (HFONT)SelectObject(hdc, f);
                        if ((run->style & STY_CODE) && it->role == ROLE_BODY) {
                            RECT chip = { fx - 3, baseY - ln->asc,
                                          fx + fr->w + 3, baseY - ln->asc + ln->h };
                            HBRUSH br = CreateSolidBrush(th->codeBg);
                            FillRect(hdc, &chip, br);
                            DeleteObject(br);
                            SetTextColor(hdc, th->codeFg);
                        } else if (run->style & STY_LINK) {
                            SetTextColor(hdc, th->link);
                        } else if (run->style & STY_IMG) {
                            SetTextColor(hdc, th->link);
                        } else if (it->role >= ROLE_H1 && it->role <= ROLE_H1 + 5) {
                            SetTextColor(hdc, th->fg);
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
                break;
            }
        }
    }
    DeleteObject(ulPen);
    DeleteObject(strikePen);
    DeleteObject(framePen);
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

static void Paint(void) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(V.hwnd, &ps);
    RECT rc;
    GetClientRect(V.hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(V.hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);

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
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(V.hwnd, &ps);
}

/* ============================ 交互 ============================ */

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
        /* 分屏：预览滚动按比例带动编辑器（锁防回环） */
        if (g_mdSplit && !g_mdSyncLock) {
            g_mdSyncLock = TRUE;
            Editor_SyncScrollFromPreview(MdView_GetScrollFraction());
            g_mdSyncLock = FALSE;
        }
    }
}

/* 命中链接：返回 href（借用，勿释放），NULL 无 */
static const wchar_t* HitLink(int px, int py) {
    int dy = py + V.scrollY;
    int dx = px;
    for (int i = 0; i < V.nItems; i++) {
        MdItem* it = &V.items[i];
        if (it->type != ITM_LINES) continue;
        if (dy < it->rc.top || dy >= it->rc.bottom) continue;
        for (int l = 0; l < it->nLines; l++) {
            MdLine* ln = &it->lines[l];
            int lt = it->rc.top + ln->y;
            if (dy < lt || dy >= lt + ln->h) continue;
            for (int k = 0; k < ln->nF; k++) {
                MdFrag* fr = &ln->frags[k];
                if (!fr->run->href || fr->len == 0) continue;
                int fx = it->rc.left + fr->x;
                if (dx >= fx - 1 && dx <= fx + fr->w + 1)
                    return fr->run->href;
            }
        }
    }
    return NULL;
}

static void OpenLink(const wchar_t* href) {
    if (!href || !*href) return;
    if (href[0] == L'#') return;   /* 文档内锚点：暂不支持 */
    if (_wcsnicmp(href, L"http://", 7) == 0 || _wcsnicmp(href, L"https://", 8) == 0 ||
        _wcsnicmp(href, L"mailto:", 7) == 0) {
        ShellExecuteW(V.hwnd, L"open", href, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    wchar_t* full = ResolveLocalHref(href);
    if (full) ShellExecuteW(V.hwnd, L"open", full, NULL, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK MdViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
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
            /* 高分辨率滚轮每次 ±40 等小增量：累积到整格再滚动 */
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
                /* 滚动条 */
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
                        /* 并排模式：Esc 只把焦点还给编辑器，不退出分屏 */
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
        case WM_SETFOCUS:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ============================ 对外接口 ============================ */

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
    ShowWindow(V.hwnd, SW_HIDE);
    V.docIdx = -1;
}

BOOL MdView_IsVisible(void) {
    return V.hwnd && IsWindowVisible(V.hwnd);
}

static void LoadContent(int index) {
    /* 释放旧模型 */
    if (V.root) { BlkFree(V.root); V.root = NULL; }
    ItemResetAll();
    V.scrollY = 0;
    V.docIdx = index;

    /* 文档目录（相对链接解析） */
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
}

void MdView_OnActivate(void) {
    if (!V.hwnd) return;
    BOOL isMd = (g_curDoc >= 0 && g_curDoc < g_docCount &&
                 g_docs[g_curDoc].lang == LANG_MD);
    BOOL show = isMd && (g_docs[g_curDoc].previewOn || g_mdSplit);
    static BOOL lastShown = FALSE;
    HWND hed = (g_curDoc >= 0 && g_curDoc < g_docCount) ? g_docs[g_curDoc].hwndEdit : NULL;
    if (show) {
        LoadContent(g_curDoc);
        if (g_mdSplit) {
            /* 并排：编辑器保持可见，焦点留给编辑器 */
            ShowWindow(V.hwnd, SW_SHOW);
            if (hed) ShowWindow(hed, SW_SHOW);
        } else {
            /* 全屏预览：必须隐藏编辑器——Scintilla 在 mdview 之后创建（Z 序更高），
               不隐藏会盖在预览之上（表现为"滚动一下才出预览"） */
            if (hed) ShowWindow(hed, SW_HIDE);
            ShowWindow(V.hwnd, SW_SHOW);
            SetFocus(V.hwnd);
        }
    } else {
        ShowWindow(V.hwnd, SW_HIDE);
        if (hed) {
            ShowWindow(hed, SW_SHOW);
            SetFocus(hed);
        }
    }
    /* 可见性翻转时让编辑区/预览区重新占位（Editor_Layout 会回调 MdView_OnLayout） */
    if (show != lastShown) {
        lastShown = show;
        Editor_Layout();
    }
    if (show) InvalidateRect(V.hwnd, NULL, FALSE);   /* 立即整幅重绘，不等首个事件 */
}

void MdView_Toggle(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    Doc* d = &g_docs[g_curDoc];
    if (d->lang != LANG_MD) return;
    d->previewOn = !d->previewOn;
    if (d->previewOn) g_mdSplit = FALSE;   /* 全屏预览与并排互斥 */
    MdView_OnActivate();   /* 内部会在可见性变化时联动 Editor_Layout */
}

void MdView_ToggleSplit(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    if (g_docs[g_curDoc].lang != LANG_MD) return;
    g_mdSplit = !g_mdSplit;
    if (g_mdSplit) g_docs[g_curDoc].previewOn = FALSE;   /* 并排时退出全屏预览 */
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

/* ============================ 打印/导出 PDF ============================ */
/* 以打印友好宽度（96dpi 基准 760px）重排后，按页高切分绘制到打印 DC。
   复用屏幕绘制管线：每页临时把 V.scrollY/V.clientH 设为该页范围。 */
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
