#include "common.h"
#include "merdagre.h"
#include "meraa.h"
#include <math.h>
static wchar_t* TrimW(wchar_t* s) {
    while (*s == L' ' || *s == L'\t') s++;
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L' ' || s[n-1] == L'\t' || s[n-1] == L'\r')) s[--n] = L'\0';
    return s;
}
static BOOL LineToWide(const char* src, int len, int* ppos, wchar_t* out, int cch) {
    int pos = *ppos, s = pos;
    while (pos < len && src[pos] != '\n') pos++;
    int lineLen = pos - s;
    if (pos < len) pos++;
    *ppos = pos;
    while (lineLen > 0 && (src[s + lineLen - 1] == '\r')) lineLen--;
    int n = MultiByteToWideChar(CP_UTF8, 0, src + s, lineLen, out, cch - 1);
    out[n ? n : 0] = L'\0';
    return n > 0;
}
static unsigned s_twGen = 0;
#define TW_CACHE_N 64
#define TW_MAX_STR 48
static struct {
    unsigned gen; HFONT f; int len; int w;
    wchar_t s[TW_MAX_STR];
} s_tw[TW_CACHE_N];
static int TextW(HDC hdc, HFONT f, const wchar_t* s) {
    int len = (int)wcslen(s);
    if (len > 0 && len < TW_MAX_STR && f) {
        unsigned __int64 h = 1469598103934665603ull;
        for (int i = 0; i < len; i++) {
            h ^= (unsigned)s[i];
            h *= 1099511628211ull;
        }
        unsigned slot = (unsigned)(h % TW_CACHE_N);
        if (s_tw[slot].gen == s_twGen && s_tw[slot].f == f &&
            s_tw[slot].len == len && wcscmp(s_tw[slot].s, s) == 0)
            return s_tw[slot].w;
        HFONT of = (HFONT)SelectObject(hdc, f);
        SIZE sz;
        GetTextExtentPoint32W(hdc, s, len, &sz);
        SelectObject(hdc, of);
        s_tw[slot].gen = s_twGen;
        s_tw[slot].f = f;
        s_tw[slot].len = len;
        s_tw[slot].w = sz.cx;
        wcscpy_s(s_tw[slot].s, TW_MAX_STR, s);
        return sz.cx;
    }
    HFONT of = (HFONT)SelectObject(hdc, f);
    SIZE sz;
    GetTextExtentPoint32W(hdc, s, (int)wcslen(s), &sz);
    SelectObject(hdc, of);
    return sz.cx;
}
static struct { HFONT f; int h; } s_fhCache[8];
static int s_fhUsed = 0;
static int FontH(HDC hdc, HFONT f) {
    for (int i = 0; i < s_fhUsed; i++)
        if (s_fhCache[i].f == f) return s_fhCache[i].h;
    HFONT of = (HFONT)SelectObject(hdc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, of);
    int slot = s_fhUsed < 8 ? s_fhUsed++ : 0;
    s_fhCache[slot].f = f;
    s_fhCache[slot].h = tm.tmHeight;
    return tm.tmHeight;
}
void Mermaid_FontsChanged(void) {
    s_fhUsed = 0;
    ZeroMemory(s_fhCache, sizeof(s_fhCache));
    s_twGen++;
}
static void LabelBrToNl(wchar_t* s) {
    wchar_t* r = s;
    wchar_t* w = s;
    while (*r) {
        if (r[0] == L'<' && (r[1] == L'b' || r[1] == L'B') && (r[2] == L'r' || r[2] == L'R')) {
            wchar_t* q = r + 3;
            while (*q == L' ' || *q == L'\t') q++;
            if (*q == L'/') q++;
            if (*q == L'>') {
                *w++ = L'\n';
                r = q + 1;
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = 0;
}
static void LabelSize(HDC hdc, HFONT font, const wchar_t* label, int* w, int* h) {
    int lines = 1;
    for (const wchar_t* p = label; *p; p++) if (*p == L'\n') lines++;
    int lh = FontH(hdc, font);
    int mw = 0;
    wchar_t buf[256];
    const wchar_t* p = label;
    for (int i = 0; i < lines; i++) {
        int n = 0;
        while (*p && *p != L'\n' && n < 255) buf[n++] = *p++;
        buf[n] = 0;
        if (*p == L'\n') p++;
        int lw = n ? TextW(hdc, font, buf) : 0;
        if (lw > mw) mw = lw;
    }
    *w = mw;
    *h = lh * lines + (lines > 1 ? UI_Scale(2) * (lines - 1) : 0);
}
typedef enum { MER_RECT, MER_ROUND, MER_CIRCLE, MER_DIAMOND, MER_FLAG,
               MER_START, MER_END, MER_STADIUM, MER_CYL } MerShape;
typedef enum { MRE_PLAIN, MRE_INHERIT, MRE_COMPOSE, MRE_AGGREG,
               MRE_ASSOC, MRE_DEPEND } MerRel;
typedef struct MerNode {
    wchar_t id[64];
    wchar_t label[256];
    MerShape shape;
    int sub;
    int rank;
    int layer;
    int x, y, w, h;
    int wFix, hFix;
    void* extra;
    int nExtraA, nExtraB;
} MerNode;
typedef struct MerEdge {
    int a, b;
    BOOL arrow, dash, thick, bidir;
    wchar_t label[160];
    wchar_t cardA[16], cardB[16];
    MerRel rel;
    BOOL markA;
    int side;
    int corrX;
    BOOL back;
    int* wps;
    int nWps;
} MerEdge;
typedef struct MerSub {
    wchar_t title[160];
    int x, y, w, h;
} MerSub;
typedef enum { MSI_MSG, MSI_NOTE, MSI_FRAME_BEGIN, MSI_FRAME_ELSE, MSI_FRAME_END } MerSeqType;
typedef enum { MSF_LOOP, MSF_ALT, MSF_OPT, MSF_PAR, MSF_CRITICAL, MSF_BREAK, MSF_RECT, MSF_ELSE } MerFrameKind;
typedef struct MerPart { wchar_t id[64], name[256]; int x; } MerPart;
typedef struct MerSeqItem {
    MerSeqType type;
    int from, to;
    BOOL dash;
    int head;
    wchar_t text[256];
    int noteSide;
    int over2;
    MerFrameKind kind;
    wchar_t label[160];
    int y, h;
} MerSeqItem;
struct MermaidDiagram {
    int kind;
    MerNode* nodes; int nNodes;
    MerEdge* edges; int nEdges;
    MerSub* subs; int nSubs;
    int dirLR, dirBT;
    MerPart* parts; int nParts;
    MerSeqItem* items; int nItems;
    void* ext;
    SIZE sz;
    /* 渲染缓存：图是静态的，滚动时内容不变，整张图只画一次，之后 BitBlt。
       GDI+ 抗锯齿比 GDI 慢一个数量级，缓存后这笔开销只在首次绘制时付一次。 */
    HBITMAP cBmp;
    HDC     cDc;
    HBITMAP cOldBmp;
    int     cW, cH;
    DWORD   cKey;
};
static void* MerGrow(void* p, int* cap, int need, int elem) {
    if (need < *cap) return p;
    int nc = *cap ? *cap * 2 : 16;
    while (nc < need) nc *= 2;
    void* q = realloc(p, (size_t)nc * elem);
    *cap = nc;
    return q;
}
static void MerCacheDrop(MermaidDiagram* d);
#define MER_CACHE_N 64
static struct { int style, w; COLORREF c; HPEN p; } s_mpc[MER_CACHE_N];
static struct { COLORREF c; HBRUSH b; } s_mbc[MER_CACHE_N];
static HPEN MerPenC(int style, int w, COLORREF c) {
    unsigned i = (((unsigned)c * 2654435761u) ^ ((unsigned)style << 6) ^ (unsigned)(w * 7))
                 % MER_CACHE_N;
    if (!s_mpc[i].p || s_mpc[i].style != style || s_mpc[i].w != w || s_mpc[i].c != c) {
        if (s_mpc[i].p) DeleteObject(s_mpc[i].p);
        s_mpc[i].p = CreatePen(style, w, c);
        s_mpc[i].style = style; s_mpc[i].w = w; s_mpc[i].c = c;
    }
    return s_mpc[i].p;
}
static HBRUSH MerBrushC(COLORREF c) {
    unsigned i = ((unsigned)c * 2654435761u) % MER_CACHE_N;
    if (!s_mbc[i].b || s_mbc[i].c != c) {
        if (s_mbc[i].b) DeleteObject(s_mbc[i].b);
        s_mbc[i].b = CreateSolidBrush(c);
        s_mbc[i].c = c;
    }
    return s_mbc[i].b;
}
typedef struct {
    MerNode* ns; int nN, capN;
    MerEdge* es; int nE, capE;
    MerSub*  ss; int nS, capS;
    int curSub;
} FlowCtx;
static int FlowFind(FlowCtx* c, const wchar_t* id) {
    for (int i = 0; i < c->nN; i++)
        if (wcscmp(c->ns[i].id, id) == 0) return i;
    return -1;
}
static int FlowAdd(FlowCtx* c, const wchar_t* id, const wchar_t* label, int shape) {
    int ex = FlowFind(c, id);
    if (ex >= 0) {
        if (label[0]) {
            wcscpy_s(c->ns[ex].label, 256, label);
            c->ns[ex].shape = shape;
            if (c->curSub >= 0) c->ns[ex].sub = c->curSub;
        }
        return ex;
    }
    c->ns = (MerNode*)MerGrow(c->ns, &c->capN, c->nN + 1, sizeof(MerNode));
    MerNode* n = &c->ns[c->nN];
    ZeroMemory(n, sizeof(*n));
    wcscpy_s(n->id, 64, id);
    wcscpy_s(n->label, 256, label[0] ? label : id);
    n->shape = shape;
    n->sub = c->curSub;
    n->rank = 0;
    return c->nN++;
}
static void FlowAddEdge(FlowCtx* c, int a, int b, BOOL arrow, BOOL dash, BOOL thick, const wchar_t* label) {
    c->es = (MerEdge*)MerGrow(c->es, &c->capE, c->nE + 1, sizeof(MerEdge));
    MerEdge* e = &c->es[c->nE++];
    ZeroMemory(e, sizeof(*e));
    e->a = a; e->b = b;
    e->arrow = arrow; e->dash = dash; e->thick = thick;
    wcscpy_s(e->label, 160, label ? label : L"");
}
static BOOL FlowParseNode(const wchar_t* line, int* pos, wchar_t* id, wchar_t* label, int* shape) {
    int p = *pos;
    while (line[p] == L' ' || line[p] == L'\t') p++;
    if (!line[p]) return FALSE;
    int i = 0;
    while (line[p] && i < 63) {
        wchar_t ch = line[p];
        if (ch == L' ' || ch == L'\t' || ch == L'[' || ch == L'{' || ch == L'(' ||
            ch == L'>' || ch == L'|' || ch == L',' || ch == L'&' || ch == L'<') break;
        if (ch == L'-' && (line[p+1] == L'-' || line[p+1] == L'.')) break;
        if (ch == L'=' && line[p+1] == L'=') break;
        id[i++] = ch; p++;
    }
    if (i == 0) return FALSE;
    id[i] = L'\0';
    *shape = MER_RECT;
    label[0] = L'\0';
    int q = p;
    while (line[q] == L' ' || line[q] == L'\t') q++;
    static const struct { const wchar_t* open; const wchar_t* close; int shape; } pairs[] = {
        { L"((", L"))", MER_CIRCLE },
        { L"[(", L")]", MER_CYL },
        { L"[/", L"/]", MER_RECT },
        { L"[\\", L"\\]", MER_RECT },
        { L"[",  L"]",  MER_RECT },
        { L"(",  L")",  MER_ROUND },
        { L"{",  L"}",  MER_DIAMOND },
        { L">",  L"]",  MER_FLAG },
    };
    for (int k = 0; k < (int)(sizeof(pairs)/sizeof(pairs[0])); k++) {
        int ol = (int)wcslen(pairs[k].open);
        if (wcsncmp(line + q, pairs[k].open, ol) != 0) continue;
        const wchar_t* end;
        /* 引号包裹的标签里允许出现闭合符（如 A["a]b"]），先按同名引号找闭合 */
        if (line[q + ol] == L'"' || line[q + ol] == L'\'') {
            const wchar_t* cq = wcschr(line + q + ol + 1, line[q + ol]);
            end = cq ? wcsstr(cq + 1, pairs[k].close) : NULL;
        } else {
            end = wcsstr(line + q + ol, pairs[k].close);
        }
        if (!end) end = wcsstr(line + q + ol, pairs[k].close);
        if (!end) break;
        int len = (int)(end - (line + q + ol));
        if (len >= 256) len = 255;
        if (len > 0 && line[q+ol] == L'"' && end[-1] == L'"' && len >= 2) {
            len -= 2; memcpy(label, line + q + ol + 1, (size_t)len * sizeof(wchar_t)); label[len] = 0;
        } else {
            memcpy(label, line + q + ol, (size_t)len * sizeof(wchar_t)); label[len] = 0;
        }
        LabelBrToNl(label);
        *shape = pairs[k].shape;
        p = (int)(end - line) + (int)wcslen(pairs[k].close);
        *pos = p;
        return TRUE;
    }
    *pos = p;
    return TRUE;
}
/* 去掉标签首尾成对的引号（mermaid 允许用 "文本" 包裹标签以容纳特殊字符） */
static void StripQuotesW(wchar_t* s) {
    size_t n = wcslen(s);
    if (n >= 2 && (s[0] == L'"' || s[0] == L'\'') && s[n - 1] == s[0]) {
        memmove(s, s + 1, (n - 1) * sizeof(wchar_t));
        s[n - 2] = L'\0';
    }
}
static BOOL FlowParseOp(const wchar_t* s, int* pos, BOOL* arrow, BOOL* dash, BOOL* thick,
                        wchar_t* label, BOOL* bidir, BOOL* rev) {
    int p = *pos;
    *bidir = FALSE;
    *rev = FALSE;
    BOOL lead = FALSE;
    if (s[p] == L'<') { lead = TRUE; p++; }
    if (!(s[p] == L'-' || s[p] == L'=')) return FALSE;
    if (s[p] == L'-' && s[p+1] != L'-' && s[p+1] != L'.') return FALSE;
    if (s[p] == L'=' && s[p+1] != L'=') return FALSE;
    int start = p;
    while (s[p] == L'-' || s[p] == L'.' || s[p] == L'=') p++;
    *arrow = FALSE;
    if (s[p] == L'>') { *arrow = TRUE; p++; }
    if (lead) {
        if (*arrow) *bidir = TRUE;
        else { *rev = TRUE; *arrow = TRUE; }
    }
    *dash = FALSE; *thick = FALSE;
    for (int q2 = start; q2 < p - (*arrow ? 1 : 0); q2++) {
        if (s[q2] == L'.') *dash = TRUE;
        if (s[q2] == L'=') *thick = TRUE;
    }
    label[0] = L'\0';
    int after = p;
    while (s[p] == L' ' || s[p] == L'\t') p++;
    if (s[p] == L'|') {
        const wchar_t* b = s + p + 1;
        const wchar_t* e;
        BOOL quoted = FALSE;
        if (*b == L'"' || *b == L'\'') {
            /* 引号包裹：内容允许出现 |，先按同名引号找闭合 */
            const wchar_t* cq = wcschr(b + 1, *b);
            if (cq) {
                quoted = TRUE;
                e = cq;
            } else {
                e = wcschr(b, L'|');
            }
        } else {
            e = wcschr(b, L'|');
        }
        if (e) {
            int len;
            const wchar_t* bar;
            if (quoted) b++; /* 跳过开引号，e 指向闭引号，中间就是内容 */
            len = (int)(e - b);
            if (len > 159) len = 159;
            if (len < 0) len = 0;
            memcpy(label, b, (size_t)len * sizeof(wchar_t));
            label[len] = 0;
            bar = quoted ? wcschr(e + 1, L'|') : e;
            p = bar ? (int)(bar - s) + 1 : (int)(e - s) + (quoted ? 1 : 0);
            *pos = p;
            return TRUE;
        }
    }
    if (!*arrow) {
        for (int j = after; s[j]; j++) {
            if (s[j] == L'>' && j > after &&
                (s[j-1] == L'-' || s[j-1] == L'.' || s[j-1] == L'=')) {
                int j2 = j;
                while (j2 > after && (s[j2-1] == L'-' || s[j2-1] == L'.' || s[j2-1] == L'=')) j2--;
                int len = j2 - after;
                while (len > 0 && (s[after + len - 1] == L' ' || s[after + len - 1] == L'\t')) len--;
                int st = after;
                while (st < after + len && (s[st] == L' ' || s[st] == L'\t')) st++;
                len -= st - after;
                if (len > 159) len = 159;
                if (len > 0) {
                    memcpy(label, s + st, (size_t)len * sizeof(wchar_t));
                    label[len] = 0;
                    StripQuotesW(label);
                }
                *arrow = TRUE;
                for (int q2 = j2; q2 < j; q2++) {
                    if (s[q2] == L'.') *dash = TRUE;
                    if (s[q2] == L'=') *thick = TRUE;
                }
                p = j + 1;
                *pos = p;
                return TRUE;
            }
        }
    }
    *pos = p;
    return TRUE;
}
static BOOL StartsWithWord(const wchar_t* s, const wchar_t* w) {
    size_t n = wcslen(w);
    if (_wcsnicmp(s, w, n) != 0) return FALSE;
    return s[n] == L'\0' || s[n] == L' ' || s[n] == L'\t';
}
static void FlowParseLine(FlowCtx* c, wchar_t* line) {
    wchar_t* s = TrimW(line);
    if (!*s || wcsncmp(s, L"%%", 2) == 0) return;
    if (StartsWithWord(s, L"classDef") || StartsWithWord(s, L"class") ||
        StartsWithWord(s, L"style") || StartsWithWord(s, L"linkStyle") ||
        StartsWithWord(s, L"click") || StartsWithWord(s, L"direction")) return;
    if (StartsWithWord(s, L"subgraph")) {
        wchar_t* rest = TrimW(s + 9);
        c->ss = (MerSub*)MerGrow(c->ss, &c->capS, c->nS + 1, sizeof(MerSub));
        MerSub* sub = &c->ss[c->nS++];
        ZeroMemory(sub, sizeof(*sub));
        const wchar_t* lb = wcschr(rest, L'[');
        const wchar_t* rb = lb ? wcschr(lb, L']') : NULL;
        if (lb && rb) {
            int len = (int)(rb - lb - 1);
            if (len > 159) len = 159;
            memcpy(sub->title, lb + 1, (size_t)len * sizeof(wchar_t));
            sub->title[len] = 0;
        } else {
            wcsncpy(sub->title, rest, 159);
            sub->title[159] = 0;
        }
        c->curSub = c->nS - 1;
        return;
    }
    if (wcscmp(s, L"end") == 0) { c->curSub = -1; return; }
    int pos = 0;
    wchar_t id[64], label[256]; int shape;
    int lhs[32], nLhs = 0;
    while (nLhs < 32) {
        if (!FlowParseNode(s, &pos, id, label, &shape)) return;
        lhs[nLhs++] = FlowAdd(c, id, label, shape);
        while (s[pos] == L' ') pos++;
        if (s[pos] == L',') { pos++; continue; }
        break;
    }
    while (s[pos] == L' ' || s[pos] == L'\t') pos++;
    BOOL hadEdge = FALSE;
    while (s[pos]) {
        BOOL arrow, dash, thick, bidir, rev; wchar_t elabel[160];
        if (!FlowParseOp(s, &pos, &arrow, &dash, &thick, elabel, &bidir, &rev)) break;
        int rhs[32], nRhs = 0;
        while (nRhs < 32) {
            if (!FlowParseNode(s, &pos, id, label, &shape)) break;
            rhs[nRhs++] = FlowAdd(c, id, label, shape);
            while (s[pos] == L' ') pos++;
            if (s[pos] == L'&') { pos++; continue; }
            break;
        }
        if (nRhs == 0) break;
        for (int i = 0; i < nLhs; i++)
            for (int j = 0; j < nRhs; j++) {
                FlowAddEdge(c, rev ? rhs[j] : lhs[i], rev ? lhs[i] : rhs[j],
                            arrow, dash, thick, elabel);
                c->es[c->nE - 1].bidir = bidir;
            }
        hadEdge = TRUE;
        nLhs = 1; lhs[0] = rhs[nRhs - 1];
        while (s[pos] == L' ' || s[pos] == L'\t') pos++;
    }
    while (s[pos] == L' ' || s[pos] == L'\t') pos++;
    if (hadEdge && s[pos] == L':' && c->nE > 0) {
        wchar_t* lab = TrimW(s + pos + 1);
        if (lab[0] == L'"') {
            wchar_t* q = wcschr(lab + 1, L'"');
            if (q) *q = 0;
            lab++;
        }
        wcsncpy(c->es[c->nE - 1].label, lab, 159);
        c->es[c->nE - 1].label[159] = 0;
    }
}
typedef struct {
    MerPart* ps; int nP, capP;
    MerSeqItem* is; int nI, capI;
    int frameStack[32]; int depth;
} SeqCtx;
static int SeqFind(SeqCtx* c, const wchar_t* id) {
    for (int i = 0; i < c->nP; i++)
        if (wcscmp(c->ps[i].id, id) == 0) return i;
    return -1;
}
static int SeqAddPart(SeqCtx* c, const wchar_t* id, const wchar_t* name) {
    int ex = SeqFind(c, id);
    if (ex >= 0) return ex;
    c->ps = (MerPart*)MerGrow(c->ps, &c->capP, c->nP + 1, sizeof(MerPart));
    MerPart* p = &c->ps[c->nP];
    ZeroMemory(p, sizeof(*p));
    wcscpy_s(p->id, 64, id);
    wcsncpy(p->name, name[0] ? name : id, 255);
    p->name[255] = 0;
    return c->nP++;
}
static MerSeqItem* SeqPush(SeqCtx* c) {
    c->is = (MerSeqItem*)MerGrow(c->is, &c->capI, c->nI + 1, sizeof(MerSeqItem));
    MerSeqItem* it = &c->is[c->nI++];
    ZeroMemory(it, sizeof(*it));
    it->from = it->to = it->over2 = -1;
    return it;
}
static BOOL SeqParseMsg(const wchar_t* s, wchar_t* from, wchar_t* to, BOOL* dash, int* head) {
    static const struct { const wchar_t* op; BOOL dash; int head; } ops[] = {
        { L"-->>", TRUE,  0 }, { L"-.->", TRUE,  0 },
        { L"--x",  TRUE,  2 }, { L"--)",  TRUE,  1 },
        { L"-->",  TRUE,  1 },
        { L"->>",  FALSE, 0 }, { L"->",   FALSE, 1 },
        { L"-x",   FALSE, 2 }, { L"-)",   FALSE, 1 },
    };
    int best = -1, bestPos = 0x7FFFFFFF;
    for (int k = 0; k < (int)(sizeof(ops)/sizeof(ops[0])); k++) {
        const wchar_t* hit = wcsstr(s, ops[k].op);
        if (hit && (hit - s) < bestPos) { bestPos = (int)(hit - s); best = k; }
    }
    if (best < 0) return FALSE;
    const wchar_t* op = ops[best].op;
    int ol = (int)wcslen(op);
    int fl = bestPos;
    while (fl > 0 && (s[fl-1] == L' ' || s[fl-1] == L'\t')) fl--;
    if (fl == 0) return FALSE;
    if (fl >= 64) fl = 63;
    memcpy(from, s, (size_t)fl * sizeof(wchar_t)); from[fl] = 0;
    const wchar_t* t = s + bestPos + ol;
    while (*t == L' ' || *t == L'\t') t++;
    int i = 0;
    while (*t && *t != L' ' && *t != L'\t' && i < 63) to[i++] = *t++;
    to[i] = 0;
    if (i == 0) return FALSE;
    *dash = ops[best].dash;
    *head = ops[best].head;
    return TRUE;
}
static void SeqParseLine(SeqCtx* c, wchar_t* line) {
    wchar_t* s = TrimW(line);
    if (!*s || wcsncmp(s, L"%%", 2) == 0) return;
    if (StartsWithWord(s, L"autonumber") || StartsWithWord(s, L"activate") ||
        StartsWithWord(s, L"deactivate") || StartsWithWord(s, L"title")) return;
    if (StartsWithWord(s, L"participant") || StartsWithWord(s, L"actor")) {
        wchar_t* rest = TrimW(s + (StartsWithWord(s, L"participant") ? 11 : 5));
        wchar_t id[64] = L"";
        int i = 0;
        while (*rest && *rest != L' ' && *rest != L'\t' && i < 63) id[i++] = *rest++;
        id[i] = 0;
        if (!i) return;
        wchar_t* name = TrimW(rest);
        if (_wcsnicmp(name, L"as ", 3) == 0) name = TrimW(name + 3);
        SeqAddPart(c, id, name);
        return;
    }
    if (StartsWithWord(s, L"Note") || StartsWithWord(s, L"note")) {
        wchar_t* rest = TrimW(s + 4);
        int side = 2; wchar_t ids[128] = L"";
        if (_wcsnicmp(rest, L"left of ", 8) == 0)  { side = 0; wcscpy_s(ids, 128, TrimW(rest + 8)); }
        else if (_wcsnicmp(rest, L"right of ", 9) == 0) { side = 1; wcscpy_s(ids, 128, TrimW(rest + 9)); }
        else if (_wcsnicmp(rest, L"over ", 5) == 0)     { side = 2; wcscpy_s(ids, 128, TrimW(rest + 5)); }
        else return;
        wchar_t* colon = wcschr(ids, L':');
        if (!colon) return;
        *colon = 0;
        wchar_t* text = TrimW(colon + 1);
        wchar_t* comma = wcschr(ids, L',');
        int over2 = -1;
        if (comma) {
            *comma = 0;
            over2 = SeqFind(c, TrimW(comma + 1));
        }
        int p = SeqFind(c, TrimW(ids));
        if (p < 0) return;
        MerSeqItem* it = SeqPush(c);
        it->type = MSI_NOTE;
        it->from = p; it->over2 = over2;
        it->noteSide = side;
        wcsncpy(it->text, text, 255);
        return;
    }
    MerFrameKind kind;
    BOOL isFrame = FALSE, isElse = FALSE;
    if      (StartsWithWord(s, L"loop"))      { kind = MSF_LOOP;      isFrame = TRUE; }
    else if (StartsWithWord(s, L"alt"))       { kind = MSF_ALT;       isFrame = TRUE; }
    else if (StartsWithWord(s, L"opt"))       { kind = MSF_OPT;       isFrame = TRUE; }
    else if (StartsWithWord(s, L"par"))       { kind = MSF_PAR;       isFrame = TRUE; }
    else if (StartsWithWord(s, L"critical"))  { kind = MSF_CRITICAL;  isFrame = TRUE; }
    else if (StartsWithWord(s, L"break"))     { kind = MSF_BREAK;     isFrame = TRUE; }
    else if (StartsWithWord(s, L"rect"))      { kind = MSF_RECT;      isFrame = TRUE; }
    else if (StartsWithWord(s, L"else") || StartsWithWord(s, L"and") ||
             StartsWithWord(s, L"option"))    { kind = MSF_ELSE;      isElse = TRUE; }
    else if (wcscmp(s, L"end") == 0) {
        if (c->depth > 0) {
            MerSeqItem* it = SeqPush(c);
            it->type = MSI_FRAME_END;
            c->depth--;
        }
        return;
    }
    if (isFrame) {
        MerSeqItem* it = SeqPush(c);
        it->type = MSI_FRAME_BEGIN;
        it->kind = kind;
        wchar_t* rest = TrimW(s);
        while (*rest && *rest != L' ' && *rest != L'\t') rest++;
        rest = TrimW(rest);
        if (_wcsnicmp(rest, L":", 1) == 0) rest = TrimW(rest + 1);
        wcsncpy(it->label, rest, 159);
        if (c->depth < 32) c->frameStack[c->depth++] = c->nI - 1;
        return;
    }
    if (isElse) {
        MerSeqItem* it = SeqPush(c);
        it->type = MSI_FRAME_ELSE;
        it->kind = MSF_ELSE;
        wchar_t* rest = TrimW(s);
        while (*rest && *rest != L' ' && *rest != L'\t') rest++;
        wcsncpy(it->label, TrimW(rest), 159);
        return;
    }
    wchar_t* colon = wcschr(s, L':');
    if (!colon) return;
    *colon = 0;
    wchar_t* text = TrimW(colon + 1);
    wchar_t from[64], to[64]; BOOL dash; int head;
    if (!SeqParseMsg(TrimW(s), from, to, &dash, &head)) return;
    int a = SeqFind(c, from);
    if (a < 0) a = SeqAddPart(c, from, from);
    int b = SeqFind(c, to);
    if (b < 0) b = SeqAddPart(c, to, to);
    MerSeqItem* it = SeqPush(c);
    it->type = MSI_MSG;
    it->from = a; it->to = b;
    it->dash = dash; it->head = head;
    wcsncpy(it->text, text, 255);
}
static BOOL ExtParse(MermaidDiagram* d, const char* src, int len, int pos);
static void ExtFree(MermaidDiagram* d);
static void ClassErFixSizeReal(MermaidDiagram* d, HDC hdc, const MdFonts* f);
static void ExtLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f);
static void ExtDraw(MermaidDiagram* d, HDC hdc, int ox, int oy,
                    const MdFonts* f, const MdTheme* th);
MermaidDiagram* Mermaid_Parse(const char* src, int len) {
    if (!src || len <= 0) return NULL;
    wchar_t line[1024];
    int pos = 0;
    int kind = -1, dirLR = 0, dirBT = 0;
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"flowchart", 9) == 0 || _wcsnicmp(s, L"graph", 5) == 0) {
            kind = 0;
            const wchar_t* d = s + (_wcsnicmp(s, L"flowchart", 9) == 0 ? 9 : 5);
            d = TrimW((wchar_t*)d);
            if (_wcsnicmp(d, L"LR", 2) == 0) dirLR = 1;
            else if (_wcsnicmp(d, L"RL", 2) == 0) { dirLR = 1; dirBT = 1; }
            else if (_wcsnicmp(d, L"BT", 2) == 0) dirBT = 1;
            break;
        }
        if (_wcsnicmp(s, L"sequenceDiagram", 15) == 0) { kind = 1; break; }
        if (_wcsnicmp(s, L"stateDiagram-v2", 15) == 0 ||
            _wcsnicmp(s, L"stateDiagram", 12) == 0) { kind = 2; break; }
        if (_wcsnicmp(s, L"classDiagram", 12) == 0) { kind = 3; break; }
        if (_wcsnicmp(s, L"erDiagram", 9) == 0) { kind = 4; break; }
        if (_wcsnicmp(s, L"pie", 3) == 0 && (s[3] == 0 || s[3] == L' ')) { kind = 5; break; }
        if (_wcsnicmp(s, L"quadrantChart", 13) == 0) { kind = 6; break; }
        if (_wcsnicmp(s, L"timeline", 8) == 0) { kind = 7; break; }
        if (_wcsnicmp(s, L"journey", 7) == 0) { kind = 8; break; }
        if (_wcsnicmp(s, L"gantt", 5) == 0) { kind = 9; break; }
        if (_wcsnicmp(s, L"xychart-beta", 12) == 0) { kind = 10; break; }
        if (_wcsnicmp(s, L"mindmap", 7) == 0) { kind = 11; break; }
        if (_wcsnicmp(s, L"gitGraph", 8) == 0) { kind = 12; break; }
        return NULL;
    }
    if (kind < 0) return NULL;
    MermaidDiagram* d = (MermaidDiagram*)calloc(1, sizeof(MermaidDiagram));
    if (!d) return NULL;
    d->kind = kind;
    d->dirLR = dirLR; d->dirBT = dirBT;
    if (kind == 0) {
        FlowCtx c; ZeroMemory(&c, sizeof(c));
        c.curSub = -1;
        while (pos < len) {
            if (!LineToWide(src, len, &pos, line, 1024)) continue;
            FlowParseLine(&c, line);
        }
        if (c.nN == 0) {
            free(c.ns); free(c.es); free(c.ss);
            free(d);
            return NULL;
        }
        d->nodes = c.ns; d->nNodes = c.nN;
        d->edges = c.es; d->nEdges = c.nE;
        d->subs  = c.ss; d->nSubs  = c.nS;
    } else if (kind == 1) {
        SeqCtx c; ZeroMemory(&c, sizeof(c));
        while (pos < len) {
            if (!LineToWide(src, len, &pos, line, 1024)) continue;
            SeqParseLine(&c, line);
        }
        while (c.depth > 0) {
            MerSeqItem* it = SeqPush(&c);
            it->type = MSI_FRAME_END;
            c.depth--;
        }
        if (c.nP == 0) {
            free(c.ps); free(c.is);
            free(d);
            return NULL;
        }
        d->parts = c.ps; d->nParts = c.nP;
        d->items = c.is; d->nItems = c.nI;
    } else {
        if (!ExtParse(d, src, len, pos)) {
            Mermaid_Free(d);
            return NULL;
        }
    }
    return d;
}
void Mermaid_Free(MermaidDiagram* d) {
    if (!d) return;
    for (int i = 0; i < d->nNodes; i++) free(d->nodes[i].extra);
    for (int e = 0; e < d->nEdges; e++) free(d->edges[e].wps);
    MerCacheDrop(d);
    ExtFree(d);
    free(d->nodes); free(d->edges); free(d->subs);
    free(d->parts); free(d->items);
    free(d);
}
static COLORREF MerPal(const MdTheme* th, int i);
static COLORREF StatePal(const MdTheme* th, int i) {
    static const int map[8] = { 0, 2, 3, 1, 4, 5, 6, 7 };
    return MerPal(th, map[i & 7]);
}
static COLORREF MixClr(COLORREF a, COLORREF b, double t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    int r = (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5);
    int g = (int)(GetGValue(a) + (GetGValue(b) - GetRValue(a)) * t + 0.5);
    int bl = (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5);
    return RGB(r, g, bl);
}
static BOOL* MerMarkBack(int n, const int* ea, const int* eb, int nE) {
    BOOL* back = (BOOL*)calloc((size_t)(nE > 0 ? nE : 1), sizeof(BOOL));
    int* head = (int*)malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    int* eit  = (int*)malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    int* nxt  = (int*)malloc(sizeof(int) * (size_t)(nE > 0 ? nE : 1));
    int* mark = (int*)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
    int* stack = (int*)malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    int* tail = (int*)malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    if (back && head && eit && nxt && mark && stack && tail) {
        for (int i = 0; i < n; i++) { head[i] = -1; tail[i] = -1; }
        for (int e = 0; e < nE; e++) {
            nxt[e] = -1;
            if (head[ea[e]] < 0) head[ea[e]] = e;
            else nxt[tail[ea[e]]] = e;
            tail[ea[e]] = e;
        }
        int top = 0;
        for (int s = 0; s < n; s++) {
            if (mark[s]) continue;
            mark[s] = 1; stack[top++] = s; eit[s] = head[s];
            while (top > 0) {
                int u = stack[top - 1];
                int e = eit[u];
                if (e < 0) { mark[u] = 2; top--; continue; }
                eit[u] = nxt[e];
                int v = eb[e];
                if (mark[v] == 1) back[e] = TRUE;
                else if (mark[v] == 0) { mark[v] = 1; stack[top++] = v; eit[v] = head[v]; }
            }
        }
    }
    free(head); free(eit); free(nxt); free(mark); free(stack); free(tail);
    return back;
}
static void MerRankLongest(int nE, const int* ea, const int* eb, const BOOL* back,
                           int* rank, int n) {
    for (int pass = 0; pass < n; pass++) {
        BOOL changed = FALSE;
        for (int e = 0; e < nE; e++) {
            if (back[e]) continue;
            if (rank[eb[e]] < rank[ea[e]] + 1) { rank[eb[e]] = rank[ea[e]] + 1; changed = TRUE; }
        }
        if (!changed) break;
    }
}
static double EdgePosAtRank(int e, const int* ea, const int* eb,
                            const int* rank, const int* pos, int q) {
    int ra = rank[ea[e]], rb = rank[eb[e]];
    int lo = ra < rb ? ra : rb, hi = ra < rb ? rb : ra;
    if (q < lo || q > hi || lo == hi) return -1.0;
    double pa = (double)pos[ea[e]], pb = (double)pos[eb[e]];
    double t = (double)(q - lo) / (double)(hi - lo);
    return pa + (pb - pa) * t;
}
static int RankPairCross(int nE, const int* ea, const int* eb, const BOOL* back,
                         const int* rank, const int* pos, int r) {
    int cr = 0;
    for (int e1 = 0; e1 < nE; e1++) {
        if (back[e1] || ea[e1] == eb[e1]) continue;
        double u1 = EdgePosAtRank(e1, ea, eb, rank, pos, r - 1);
        if (u1 < 0) continue;
        double v1 = EdgePosAtRank(e1, ea, eb, rank, pos, r);
        for (int e2 = e1 + 1; e2 < nE; e2++) {
            if (back[e2] || ea[e2] == eb[e2]) continue;
            double u2 = EdgePosAtRank(e2, ea, eb, rank, pos, r - 1);
            if (u2 < 0) continue;
            double v2 = EdgePosAtRank(e2, ea, eb, rank, pos, r);
            if ((u1 < u2 && v1 > v2) || (u1 > u2 && v1 < v2)) cr++;
        }
    }
    return cr;
}
static void MerOrderRanks(int n, int nE, const int* ea, const int* eb, const BOOL* back,
                          int nr, const int* rank, int* pos, int* key) {
    int* cnt = (int*)calloc((size_t)nr, sizeof(int));
    int** buckets = (int**)calloc((size_t)nr, sizeof(int*));
    int* fill = (int*)calloc((size_t)nr, sizeof(int));
    if (!cnt || !buckets || !fill) { free(cnt); free(buckets); free(fill); return; }
    for (int i = 0; i < n; i++) cnt[rank[i]]++;
    for (int r = 0; r < nr; r++)
        buckets[r] = (int*)malloc(sizeof(int) * (size_t)(cnt[r] ? cnt[r] : 1));
    for (int i = 0; i < n; i++) buckets[rank[i]][fill[rank[i]]++] = i;
    for (int r = 0; r < nr; r++)
        for (int k = 0; k < cnt[r]; k++) pos[buckets[r][k]] = k;
    for (int sweep = 0; sweep < 4; sweep++) {
        for (int r = 1; r < nr; r++) {
            for (int k = 0; k < cnt[r]; k++) {
                int i = buckets[r][k];
                int sum = 0, c = 0;
                for (int e = 0; e < nE; e++) {
                    if (back[e]) continue;
                    if (eb[e] == i && rank[ea[e]] < r) { sum += pos[ea[e]]; c++; }
                    if (ea[e] == i && rank[eb[e]] < r) { sum += pos[eb[e]]; c++; }
                }
                key[i] = (c > 0) ? (int)(sum * 1000 / c) : k * 1000 + k;
            }
            for (int k2 = 1; k2 < cnt[r]; k2++) {
                int cur = buckets[r][k2];
                int kv = key[cur];
                int k3 = k2 - 1;
                while (k3 >= 0 && key[buckets[r][k3]] > kv) {
                    buckets[r][k3 + 1] = buckets[r][k3];
                    k3--;
                }
                buckets[r][k3 + 1] = cur;
            }
            for (int k2 = 0; k2 < cnt[r]; k2++) pos[buckets[r][k2]] = k2;
        }
        for (int r = nr - 2; r >= 0; r--) {
            for (int k = 0; k < cnt[r]; k++) {
                int i = buckets[r][k];
                int sum = 0, c = 0;
                for (int e = 0; e < nE; e++) {
                    if (back[e]) continue;
                    if (eb[e] == i && rank[ea[e]] > r) { sum += pos[ea[e]]; c++; }
                    if (ea[e] == i && rank[eb[e]] > r) { sum += pos[eb[e]]; c++; }
                }
                key[i] = (c > 0) ? (int)(sum * 1000 / c) : k * 1000 + k;
            }
            for (int k2 = 1; k2 < cnt[r]; k2++) {
                int cur = buckets[r][k2];
                int kv = key[cur];
                int k3 = k2 - 1;
                while (k3 >= 0 && key[buckets[r][k3]] > kv) {
                    buckets[r][k3 + 1] = buckets[r][k3];
                    k3--;
                }
                buckets[r][k3 + 1] = cur;
            }
            for (int k2 = 0; k2 < cnt[r]; k2++) pos[buckets[r][k2]] = k2;
        }
    }
    for (int pass = 0; pass < 4; pass++) {
        BOOL changed = FALSE;
        for (int r = 0; r < nr; r++) {
            for (int k = 0; k + 1 < cnt[r]; k++) {
                int c0 = RankPairCross(nE, ea, eb, back, rank, pos, r) +
                         RankPairCross(nE, ea, eb, back, rank, pos, r + 1);
                int t = buckets[r][k];
                buckets[r][k] = buckets[r][k + 1];
                buckets[r][k + 1] = t;
                pos[buckets[r][k]] = k;
                pos[buckets[r][k + 1]] = k + 1;
                int c1 = RankPairCross(nE, ea, eb, back, rank, pos, r) +
                         RankPairCross(nE, ea, eb, back, rank, pos, r + 1);
                if (c1 < c0) {
                    changed = TRUE;
                } else {
                    t = buckets[r][k];
                    buckets[r][k] = buckets[r][k + 1];
                    buckets[r][k + 1] = t;
                    pos[buckets[r][k]] = k;
                    pos[buckets[r][k + 1]] = k + 1;
                }
            }
        }
        if (!changed) break;
    }
    for (int r = 0; r < nr; r++) free(buckets[r]);
    free(cnt); free(buckets); free(fill);
}
typedef struct {
    int sub;
    int m0, mn;
    int x, y, w, h;
    int rank;
    int rMin, rMax, regX, regW, colW;
} MerClu;
static BOOL MerIvHit(const int* iv, int nI, int lo, int hi) {
    for (int k = 0; k < nI; k++)
        if (lo < iv[k * 2 + 1] && hi > iv[k * 2]) return TRUE;
    return FALSE;
}
static BOOL MerLinked(int nE, const int* ea, const int* eb, int i, int j) {
    for (int e = 0; e < nE; e++)
        if ((ea[e] == i && eb[e] == j) || (ea[e] == j && eb[e] == i)) return TRUE;
    return FALSE;
}
/* dagre 移植布局：与 mermaid.js（dagre 渲染器）同构。
 * 1 = 用本实现，0 = 退回原来的手写规则布局。 */
static int g_merDagre = 1;

static void FlowLayoutDagre(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    int n = d->nNodes, nSub = d->nSubs, nE = d->nEdges;
    int margin = UI_Scale(12);
    (void)hdc;
    (void)f;
    MdgGraph g;
    int* map;
    int* subMap;
    int* eIdx;
    int i, e;
    int minX = 0x7FFFFFFF, minY = 0x7FFFFFFF;
    /* dagre 处理 rankdir=LR/RL 的办法：布局前把每个节点的宽高对调，全套 TB 算法
       照跑，输出时再把坐标的 x/y 换回来。所以这里只需要跟着换，不用改布局内核。 */
    int lr = d->dirLR;

    if (n <= 0) {
        d->sz.cx = UI_Scale(60);
        d->sz.cy = UI_Scale(40);
        return;
    }

    map = (int*)malloc(sizeof(int) * (size_t)n);
    subMap = (int*)malloc(sizeof(int) * (size_t)(nSub > 0 ? nSub : 1));
    eIdx = (int*)malloc(sizeof(int) * (size_t)(nE > 0 ? nE : 1));
    if (!map || !subMap || !eIdx) {
        free(map);
        free(subMap);
        free(eIdx);
        d->sz.cx = UI_Scale(60);
        d->sz.cy = UI_Scale(40);
        return;
    }

    mdg_init(&g);
    g.nodesep = UI_Scale(50);
    g.ranksep = UI_Scale(50);
    g.edgesep = UI_Scale(20);

    for (i = 0; i < n; i++)
        map[i] = mdg_add_node(&g, lr ? d->nodes[i].h : d->nodes[i].w,
                              lr ? d->nodes[i].w : d->nodes[i].h, -1);
    for (i = 0; i < nSub; i++) {
        subMap[i] = mdg_add_node(&g, 0, 0, -1);
        g.n[subMap[i]].type = MDG_SUB;
    }
    for (i = 0; i < n; i++) {
        int s = d->nodes[i].sub;
        if (s >= 0 && s < nSub) g.n[map[i]].parent = subMap[s];
    }
    for (e = 0; e < nE; e++) {
        int a = d->edges[e].a, b = d->edges[e].b;
        eIdx[e] = -1;
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) continue;
        eIdx[e] = mdg_add_edge(&g, map[a], map[b]);
    }

    mdg_layout(&g);

    for (i = 0; i < n; i++) {
        int k = map[i];
        int cx = lr ? g.n[k].y : g.n[k].x;
        int cy = lr ? g.n[k].x : g.n[k].y;
        d->nodes[i].x = cx - d->nodes[i].w / 2;
        d->nodes[i].y = cy - d->nodes[i].h / 2;
        d->nodes[i].rank = g.n[k].rank;
        d->nodes[i].layer = g.n[k].rank; /* 与老布局的约定保持一致（layer==rank） */
        if (d->nodes[i].x < minX) minX = d->nodes[i].x;
        if (d->nodes[i].y < minY) minY = d->nodes[i].y;
    }
    /* 自环固定画在节点上方（FlowDraw 里 18px + 余量），顶层节点要留出这段空间，
       否则自环会被裁到画布外 */
    for (e = 0; e < nE; e++) {
        int a = d->edges[e].a;
        if (a < 0 || a >= n || d->edges[e].b != a) continue;
        if (d->nodes[a].y - UI_Scale(22) < minY) minY = d->nodes[a].y - UI_Scale(22);
    }
    for (i = 0; i < nSub; i++) {
        /* 子图框直接取布局输出（border 节点界定，与 mermaid/dagre 一致），
           不再用成员包围盒，否则相邻子图框会互相压住 */
        int k = subMap[i];
        int cw = lr ? g.n[k].h : g.n[k].w; /* 布局里的宽高是换过的，换回来 */
        int ch = lr ? g.n[k].w : g.n[k].h;
        int cx = lr ? g.n[k].y : g.n[k].x;
        int cy = lr ? g.n[k].x : g.n[k].y;
        if (cw <= 0 || ch <= 0) {
            d->subs[i].w = 0;
            d->subs[i].h = 0;
            continue;
        }
        d->subs[i].x = cx - cw / 2;
        d->subs[i].y = cy - ch / 2;
        d->subs[i].w = cw;
        d->subs[i].h = ch;
        if (d->subs[i].x < minX) minX = d->subs[i].x;
        if (d->subs[i].y < minY) minY = d->subs[i].y;
    }

    {
        int shiftX = 0, shiftY = 0;
        if (minX < margin || minY < margin) {
            shiftX = (minX < margin) ? (margin - minX) : 0;
            shiftY = (minY < margin) ? (margin - minY) : 0;
            for (i = 0; i < n; i++) {
                d->nodes[i].x += shiftX;
                d->nodes[i].y += shiftY;
            }
            for (i = 0; i < nSub; i++) {
                d->subs[i].x += shiftX;
                d->subs[i].y += shiftY;
            }
        }
        /* 边的折点：normalize 打断出来的 dummy 链坐标，即 dagre 的
           originalEdge.points。折点与节点同一坐标系，要跟着一起平移。 */
        for (e = 0; e < nE; e++) {
            int buf[128];
            int np, k;
            free(d->edges[e].wps);
            d->edges[e].wps = NULL;
            d->edges[e].nWps = 0;
            if (eIdx[e] < 0) continue;
            np = mdg_edge_waypoints(&g, eIdx[e], buf, 64);
            if (np <= 0) continue;
            d->edges[e].wps = (int*)malloc(sizeof(int) * 2 * (size_t)np);
            if (!d->edges[e].wps) continue;
            for (k = 0; k < np; k++) {
                d->edges[e].wps[k * 2] = (lr ? buf[k * 2 + 1] : buf[k * 2]) + shiftX;
                d->edges[e].wps[k * 2 + 1] = (lr ? buf[k * 2] : buf[k * 2 + 1]) + shiftY;
            }
            d->edges[e].nWps = np;
        }
    }

    {
        int maxX = 0, maxY = 0;
        for (i = 0; i < n; i++) {
            if (d->nodes[i].x + d->nodes[i].w > maxX) maxX = d->nodes[i].x + d->nodes[i].w;
            if (d->nodes[i].y + d->nodes[i].h > maxY) maxY = d->nodes[i].y + d->nodes[i].h;
        }
        for (i = 0; i < nSub; i++) {
            if (d->subs[i].w <= 0) continue;
            if (d->subs[i].x + d->subs[i].w > maxX) maxX = d->subs[i].x + d->subs[i].w;
            if (d->subs[i].y + d->subs[i].h > maxY) maxY = d->subs[i].y + d->subs[i].h;
        }
        d->sz.cx = maxX + margin;
        d->sz.cy = maxY + margin;
    }

    free(map);
    free(subMap);
    free(eIdx);
    mdg_free(&g);
}

static void FlowLayoutCompound(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    if (g_merDagre) {
        FlowLayoutDagre(d, hdc, f);
        return;
    }

    int n = d->nNodes;
    int margin = UI_Scale(12);
    int hgap = UI_Scale(88), vgap = UI_Scale(96);
    int sp = UI_Scale(16), titleH = UI_Scale(20);
    int vw = UI_Scale(6);
    int nSub = d->nSubs;
    int nE = d->nEdges;
    int nEn = nE > 0 ? nE : 1;
    int nNn = n > 0 ? n : 1;
    int* nea = (int*)malloc(sizeof(int) * (size_t)nEn);
    int* neb = (int*)malloc(sizeof(int) * (size_t)nEn);
    int* grank = (int*)calloc((size_t)nNn, sizeof(int));
    if (!nea || !neb || !grank) {
        free(nea); free(neb); free(grank);
        d->sz.cx = UI_Scale(60); d->sz.cy = UI_Scale(40);
        return;
    }
    for (int e = 0; e < nE; e++) { nea[e] = d->edges[e].a; neb[e] = d->edges[e].b; }
    BOOL* gback = MerMarkBack(n, nea, neb, nE);
    MerRankLongest(nE, nea, neb, gback, grank, n);
    for (int e = 0; e < nE; e++) d->edges[e].back = gback[e];
    int* cluOf = (int*)malloc(sizeof(int) * (size_t)nNn);
    int freeRun = 0;
    for (int i = 0; i < n; i++)
        cluOf[i] = (d->nodes[i].sub >= 0) ? d->nodes[i].sub : nSub + freeRun++;
    int nC = nSub + freeRun;
    {
        int* loR = (int*)malloc(sizeof(int) * (size_t)nNn);
        int* hiR = (int*)malloc(sizeof(int) * (size_t)nNn);
        int* cMax = (int*)calloc((size_t)nC, sizeof(int));
        if (loR && hiR && cMax) {
            for (int i = 0; i < n; i++) { loR[i] = 0; hiR[i] = 0x7FFFFFFF; }
            for (int e = 0; e < nE; e++) {
                if (gback[e]) continue;
                int pa = nea[e], pb = neb[e];
                if (grank[pa] + 1 > loR[pb]) loR[pb] = grank[pa] + 1;
                if (grank[pb] - 1 < hiR[pa]) hiR[pa] = grank[pb] - 1;
            }
            for (int i = 0; i < n; i++)
                if (grank[i] > cMax[cluOf[i]]) cMax[cluOf[i]] = grank[i];
            for (int i = 0; i < n; i++) {
                if (hiR[i] <= loR[i]) continue;
                int cc = cluOf[i];
                int alone = 1;
                for (int j2 = 0; j2 < n && alone; j2++)
                    if (j2 != i && cluOf[j2] == cc && grank[j2] == grank[i]) alone = 0;
                if (!alone) continue;
                int t2 = cMax[cc];
                if (t2 > hiR[i]) t2 = hiR[i];
                if (t2 < loR[i]) t2 = loR[i];
                grank[i] = t2;
            }
        }
        free(loR); free(hiR); free(cMax);
    }
    {
        int* outC = (int*)calloc((size_t)nNn, sizeof(int));
        int* inC = (int*)calloc((size_t)nNn, sizeof(int));
        int* ord = (int*)malloc(sizeof(int) * (size_t)nNn);
        if (outC && inC && ord) {
            for (int e = 0; e < nE; e++) {
                if (gback[e] || nea[e] == neb[e]) continue;
                outC[nea[e]]++;
                inC[neb[e]]++;
            }
            for (int i = 0; i < n; i++) ord[i] = i;
            for (int i2 = 1; i2 < n; i2++) {
                int cur = ord[i2];
                int j2 = i2 - 1;
                while (j2 >= 0 && grank[ord[j2]] < grank[cur]) { ord[j2 + 1] = ord[j2]; j2--; }
                ord[j2 + 1] = cur;
            }
            for (int q = 0; q < n; q++) {
                int i = ord[q];
                if (outC[i] == 0 || outC[i] <= inC[i]) continue;
                int minSpan = 0x7FFFFFFF;
                for (int e = 0; e < nE; e++) {
                    if (gback[e] || nea[e] != i || neb[e] == i) continue;
                    int sp2 = grank[neb[e]] - grank[i];
                    if (sp2 < minSpan) minSpan = sp2;
                }
                if (minSpan > 1 && minSpan < 0x7FFFFFFF) grank[i] += minSpan - 1;
            }
        }
        free(outC); free(inC); free(ord);
    }
    int nr = 1;
    for (int i = 0; i < n; i++) {
        d->nodes[i].rank = grank[i];
        d->nodes[i].layer = grank[i];
        if (grank[i] + 1 > nr) nr = grank[i] + 1;
    }
    MerClu* cl = (MerClu*)calloc((size_t)nC, sizeof(MerClu));
    int* memb = (int*)malloc(sizeof(int) * (size_t)nNn);
    int* mfill = (int*)calloc((size_t)nC, sizeof(int));
    if (!cluOf || !cl || !memb || !mfill) {
        free(cluOf); free(cl); free(memb); free(mfill);
        free(gback); free(grank); free(nea); free(neb);
        d->sz.cx = UI_Scale(60); d->sz.cy = UI_Scale(40);
        return;
    }
    for (int i = 0; i < n; i++) cl[cluOf[i]].mn++;
    for (int c = 0; c < nC; c++) {
        cl[c].sub = (c < nSub) ? c : -1;
        cl[c].rMin = 0x7FFFFFFF;
        cl[c].rMax = -1;
    }
    int acc = 0;
    for (int c = 0; c < nC; c++) { cl[c].m0 = acc; acc += cl[c].mn; }
    for (int i = 0; i < n; i++) {
        int c = cluOf[i];
        memb[cl[c].m0 + mfill[c]++] = i;
        if (grank[i] < cl[c].rMin) cl[c].rMin = grank[i];
        if (grank[i] > cl[c].rMax) cl[c].rMax = grank[i];
    }
    int* cea = NULL, *ceb = NULL;
    int nCE = 0, capCEa = 0, capCEb = 0;
    for (int e = 0; e < nE; e++) {
        if (gback[e]) continue;
        int ca = cluOf[nea[e]], cb = cluOf[neb[e]];
        if (ca == cb) continue;
        cea = (int*)MerGrow(cea, &capCEa, nCE + 1, sizeof(int));
        ceb = (int*)MerGrow(ceb, &capCEb, nCE + 1, sizeof(int));
        if (cea && ceb) { cea[nCE] = ca; ceb[nCE] = cb; }
        nCE++;
    }
    int* clvl = (int*)malloc(sizeof(int) * (size_t)nC);
    for (int c = 0; c < nC; c++) clvl[c] = (cl[c].mn > 0) ? cl[c].rMin : 0;
    int nrL = 1;
    for (int c = 0; c < nC; c++) if (clvl[c] + 1 > nrL) nrL = clvl[c] + 1;
    int* cpos = (int*)malloc(sizeof(int) * (size_t)nC);
    int* ckey = (int*)malloc(sizeof(int) * (size_t)nC);
    BOOL* cback = MerMarkBack(nC, cea, ceb, nCE);
    if (cpos && ckey && cback)
        MerOrderRanks(nC, nCE, cea, ceb, cback, nrL, clvl, cpos, ckey);
    else
        for (int c = 0; c < nC; c++) if (cpos) cpos[c] = c;
    int* regOrder = (int*)malloc(sizeof(int) * (size_t)nC);
    if (regOrder) {
        for (int c = 0; c < nC; c++) regOrder[c] = c;
        for (int i2 = 1; i2 < nC; i2++) {
            int cur = regOrder[i2];
            int kl = clvl[cur], kp = cpos[cur];
            int j2 = i2 - 1;
            while (j2 >= 0) {
                int o = regOrder[j2];
                if (clvl[o] > kl || (clvl[o] == kl && cpos[o] > kp)) {
                    regOrder[j2 + 1] = regOrder[j2];
                    j2--;
                } else break;
            }
            regOrder[j2 + 1] = cur;
        }
    }
    int vG = 0;
    for (int e = 0; e < nE; e++) {
        if (gback[e]) continue;
        int sp2 = grank[neb[e]] - grank[nea[e]];
        if (sp2 > 1) vG += sp2 - 1;
    }
    int vCap = vG > 0 ? vG : 1;
    int nA = n + vG;
    int* wpx = (int*)malloc(sizeof(int) * (size_t)vCap);
    int* wpy = (int*)malloc(sizeof(int) * (size_t)vCap);
    int* vexG = (int*)malloc(sizeof(int) * (size_t)vCap);
    int* vrkG = (int*)malloc(sizeof(int) * (size_t)vCap);
    int* vreg = (int*)malloc(sizeof(int) * (size_t)vCap);
    int* irankA = (int*)calloc((size_t)(nA > 0 ? nA : 1), sizeof(int));
    int* aea = NULL, *aeb = NULL, *abk = NULL;
    int nAE = 0, capAEa = 0, capAEb = 0, capAEk = 0;
    int vRun = 0;
    for (int i = 0; i < n; i++) irankA[i] = grank[i];
    for (int e = 0; e < nE; e++) {
        int span = grank[neb[e]] - grank[nea[e]];
        if (gback[e] || span <= 1) {
            aea = (int*)MerGrow(aea, &capAEa, nAE + 1, sizeof(int));
            aeb = (int*)MerGrow(aeb, &capAEb, nAE + 1, sizeof(int));
            abk = (int*)MerGrow(abk, &capAEk, nAE + 1, sizeof(int));
            if (aea && aeb && abk) {
                aea[nAE] = nea[e]; aeb[nAE] = neb[e]; abk[nAE] = gback[e];
            }
            nAE++;
            continue;
        }
        int regC = (cluOf[nea[e]] == cluOf[neb[e]]) ? cluOf[nea[e]] : -1;
        int prev = nea[e];
        for (int rr = grank[nea[e]] + 1; rr < grank[neb[e]]; rr++) {
            int vi = n + vRun;
            irankA[vi] = rr;
            vexG[vRun] = e; vrkG[vRun] = rr; vreg[vRun] = regC;
            aea = (int*)MerGrow(aea, &capAEa, nAE + 1, sizeof(int));
            aeb = (int*)MerGrow(aeb, &capAEb, nAE + 1, sizeof(int));
            abk = (int*)MerGrow(abk, &capAEk, nAE + 1, sizeof(int));
            if (aea && aeb && abk) {
                aea[nAE] = prev; aeb[nAE] = vi; abk[nAE] = FALSE;
            }
            nAE++;
            prev = vi;
            vRun++;
        }
        aea = (int*)MerGrow(aea, &capAEa, nAE + 1, sizeof(int));
        aeb = (int*)MerGrow(aeb, &capAEb, nAE + 1, sizeof(int));
        abk = (int*)MerGrow(abk, &capAEk, nAE + 1, sizeof(int));
        if (aea && aeb && abk) {
            aea[nAE] = prev; aeb[nAE] = neb[e]; abk[nAE] = FALSE;
        }
        nAE++;
    }
    int* iposA = (int*)malloc(sizeof(int) * (size_t)(nA > 0 ? nA : 1));
    int* ikeyA = (int*)malloc(sizeof(int) * (size_t)(nA > 0 ? nA : 1));
    if (iposA && ikeyA)
        MerOrderRanks(nA, nAE, aea, aeb, abk, nr, irankA, iposA, ikeyA);
    else
        for (int i = 0; i < nA; i++) if (iposA) iposA[i] = i;
    int* rankMain = (int*)calloc((size_t)nr, sizeof(int));
    int* mainBase = (int*)calloc((size_t)nr, sizeof(int));
    for (int i = 0; i < n; i++) {
        int msz = d->dirLR ? d->nodes[i].w : d->nodes[i].h;
        if (msz > rankMain[grank[i]]) rankMain[grank[i]] = msz;
    }
    int mp = margin;
    for (int r = 0; r < nr; r++) { mainBase[r] = mp; mp += rankMain[r] + vgap; }
    int mainTotal = mp - margin - vgap;
    int* items = (int*)malloc(sizeof(int) * (size_t)(nA > 0 ? nA : 1));
    /* 列式分组预计算：成员较多、跨多行的子图按「入口 → 调用链」拆成纵向列。
     * 必须在簇宽统计之前定下来，否则宽度仍按两列估，新列会被压扁。 */
    int* gK = (int*)calloc((size_t)nC, sizeof(int));
    int* gCol = (int*)calloc((size_t)nNn, sizeof(int));
    int* gCW = (int*)calloc((size_t)nC * 4, sizeof(int));
    int* gOrd = (int*)calloc((size_t)nC * 4, sizeof(int));
    if (gK && gCol && gCW && gOrd && !d->dirLR) {
        int ent[16], entRank[16], entDecl[16], colOf[16], nEnt, kk;
        unsigned int reach[16];
        for (int c = 0; c < nC; c++) {
            if (cl[c].mn < 6 || cl[c].mn > 16 || cl[c].rMax - cl[c].rMin < 2) continue;
            nEnt = 0;
            for (kk = 0; kk < cl[c].mn; kk++) {
                int ni = memb[cl[c].m0 + kk];
                BOOL hasPar = FALSE;
                for (int e = 0; e < nE && !hasPar; e++) {
                    if (neb[e] != ni) continue;
                    int p = nea[e];
                    if (p == ni || cluOf[p] != c) continue;
                    if (grank[p] < grank[ni]) hasPar = TRUE;
                }
                if (!hasPar) {
                    ent[nEnt] = kk;
                    entRank[nEnt] = grank[ni];
                    entDecl[nEnt] = ni;
                    nEnt++;
                }
            }
            if (nEnt < 2 || nEnt > 4) continue;
            for (kk = 0; kk < cl[c].mn; kk++) reach[kk] = 0;
            for (int q = 0; q < nEnt; q++) reach[ent[q]] = 1u << q;
            for (int r = cl[c].rMin; r <= cl[c].rMax; r++) {
                for (kk = 0; kk < cl[c].mn; kk++) {
                    int ni = memb[cl[c].m0 + kk];
                    if (grank[ni] != r || reach[kk] == 0) continue;
                    for (int e = 0; e < nE; e++) {
                        if (nea[e] != ni) continue;
                        int t = neb[e];
                        if (t == ni || cluOf[t] != c || grank[t] <= grank[ni]) continue;
                        for (int k3 = 0; k3 < cl[c].mn; k3++) {
                            if (memb[cl[c].m0 + k3] != t) continue;
                            reach[k3] |= reach[kk];
                            break;
                        }
                    }
                }
            }
            for (kk = 0; kk < cl[c].mn; kk++) {
                int best = -1, bestR = -1, bestD = -1;
                for (int q = 0; q < nEnt; q++) {
                    if (ent[q] == kk) { best = q; break; }
                    if (!(reach[kk] & (1u << q))) continue;
                    if (entRank[q] > bestR ||
                        (entRank[q] == bestR && entDecl[q] > bestD)) {
                        best = q;
                        bestR = entRank[q];
                        bestD = entDecl[q];
                    }
                }
                colOf[kk] = best;
            }
            for (kk = 0; kk < cl[c].mn; kk++) {
                if (colOf[kk] >= 0) continue;
                int pick = 0, ni = memb[cl[c].m0 + kk];
                for (int e = 0; e < nE; e++) {
                    int other = -1;
                    if (nea[e] == ni) other = neb[e];
                    else if (neb[e] == ni) other = nea[e];
                    if (other < 0 || other == ni || cluOf[other] != c) continue;
                    for (int k3 = 0; k3 < cl[c].mn; k3++) {
                        if (memb[cl[c].m0 + k3] != other || colOf[k3] < 0) continue;
                        pick = colOf[k3];
                        e = nE;
                        break;
                    }
                }
                colOf[kk] = pick;
            }
            {
                int csz[4], cw2[4], ordc[4];
                for (int q = 0; q < nEnt; q++) { csz[q] = 0; cw2[q] = 0; ordc[q] = q; }
                for (kk = 0; kk < cl[c].mn; kk++) {
                    int q = colOf[kk];
                    if (q < 0 || q >= nEnt) continue;
                    csz[q]++;
                    if (d->nodes[memb[cl[c].m0 + kk]].w > cw2[q])
                        cw2[q] = d->nodes[memb[cl[c].m0 + kk]].w;
                }
                for (int p2 = 1; p2 < nEnt; p2++) {
                    int cur = ordc[p2], p3 = p2 - 1;
                    while (p3 >= 0 &&
                           (csz[ordc[p3]] > csz[cur] ||
                            (csz[ordc[p3]] == csz[cur] && entDecl[ordc[p3]] > entDecl[cur]))) {
                        ordc[p3 + 1] = ordc[p3];
                        p3--;
                    }
                    ordc[p3 + 1] = cur;
                }
                gK[c] = nEnt;
                for (kk = 0; kk < cl[c].mn; kk++)
                    gCol[memb[cl[c].m0 + kk]] = colOf[kk];
                for (int q = 0; q < nEnt; q++) {
                    gCW[c * 4 + q] = cw2[q];
                    gOrd[c * 4 + q] = ordc[q];
                }
            }
        }
    }
    for (int c = 0; c < nC; c++) {
        BOOL real = (cl[c].sub >= 0);
        int maxCW = 0;
        for (int r = cl[c].rMin; r <= cl[c].rMax; r++) {
            int cnt2 = 0, cw = 0, nNode2 = 0, nChan2 = 0;
            for (int k = 0; k < cl[c].mn; k++) {
                int i = memb[cl[c].m0 + k];
                if (grank[i] != r) continue;
                items[cnt2++] = i;
                cw += (d->dirLR ? d->nodes[i].h : d->nodes[i].w);
                nNode2++;
            }
            for (int v = 0; v < vG; v++) {
                if (vreg[v] != c || vrkG[v] != r) continue;
                items[cnt2++] = n + v;
                cw += vw;
                nChan2++;
            }
            if (cnt2 > 1) {
                int vgap2 = hgap * 2 / 5;
                int pairs = cnt2 - 1;
                int cross = nNode2 * nChan2;
                if (cross > pairs) cross = pairs;
                cw += vgap2 * cross + hgap * (pairs - cross);
            }
            if (cnt2 > 0 && cw > maxCW) maxCW = cw;
        }
        if (gK[c] >= 2) {
            int gw3 = hgap * (gK[c] - 1);
            for (int q = 0; q < gK[c]; q++) gw3 += gCW[c * 4 + q];
            if (gw3 > maxCW) maxCW = gw3;
        }
        cl[c].regW = maxCW + (real ? ((d->dirLR ? titleH : 0) + 2 * sp) : 0);
        if (real) {
            int tw = d->subs[cl[c].sub].title[0]
                ? TextW(hdc, f->sm, d->subs[cl[c].sub].title) + sp * 2 : 0;
            if (cl[c].mn == 0) {
                int w2 = tw > UI_Scale(60) ? tw : UI_Scale(60);
                if (w2 > cl[c].regW) cl[c].regW = w2;
            } else if (tw > cl[c].regW) {
                cl[c].regW = tw;
            }
        }
    }
    char* occ = (char*)calloc((size_t)nr, 1);
    int crossCur = margin;
    for (int q = 0; q < nC && regOrder && occ; q++) {
        int c = regOrder[q];
        if (cl[c].rMax < 0) continue;
        BOOL clashR = FALSE;
        for (int r = cl[c].rMin; r <= cl[c].rMax; r++)
            if (occ[r]) { clashR = TRUE; break; }
        if (!clashR) crossCur = margin;
        cl[c].regX = crossCur;
        int nxt = cl[c].regX + cl[c].regW + hgap;
        if (nxt > crossCur) crossCur = nxt;
        for (int r = cl[c].rMin; r <= cl[c].rMax; r++) occ[r] = 1;
    }
    if (!d->dirLR) {
        for (int c = 0; c < nC; c++) {
            if (cl[c].rMax < 0 || cl[c].sub < 0) continue;
            int gw2 = 0;
            for (int c2 = 0; c2 < nC; c2++)
                if (cl[c2].rMax >= 0 && cl[c2].sub >= 0 &&
                    cl[c2].regX == cl[c].regX && cl[c2].regW > gw2)
                    gw2 = cl[c2].regW;
            cl[c].colW = gw2;
        }
    }
    for (int c = 0; c < nC; c++) {
        BOOL real = (cl[c].sub >= 0);
        if (cl[c].rMax < 0) continue;
        for (int r = cl[c].rMin; r <= cl[c].rMax; r++) {
            int cnt2 = 0;
            for (int k = 0; k < cl[c].mn; k++) {
                int i = memb[cl[c].m0 + k];
                if (grank[i] == r) items[cnt2++] = i;
            }
            for (int v = 0; v < vG; v++)
                if (vreg[v] == c && vrkG[v] == r) items[cnt2++] = n + v;
            for (int p2 = 1; p2 < cnt2; p2++) {
                int cur = items[p2];
                int kv;
                if (cur < n) kv = cur * 1000;
                else {
                    int vi = cur - n, ei = vexG[vi];
                    kv = (nea[ei] + neb[ei]) * 500 + vi;
                }
                int p3 = p2 - 1;
                while (p3 >= 0) {
                    int ov = items[p3];
                    int kv2;
                    if (ov < n) kv2 = ov * 1000;
                    else {
                        int vi2 = ov - n, ei2 = vexG[vi2];
                        kv2 = (nea[ei2] + neb[ei2]) * 500 + vi2;
                    }
                    if (kv2 > kv) { items[p3 + 1] = items[p3]; p3--; }
                    else break;
                }
                items[p3 + 1] = cur;
            }
            int contentW = 0;
            for (int k2 = 0; k2 < cnt2; k2++) {
                int it = items[k2];
                if (it < n)
                    contentW += (d->dirLR ? d->nodes[it].h : d->nodes[it].w);
                else
                    contentW += vw;
                if (k2 + 1 < cnt2) {
                    int nx2 = items[k2 + 1];
                    int gap2 = hgap;
                    if (it >= n || nx2 >= n) gap2 = hgap * 2 / 5;
                    else if (!MerLinked(nE, nea, neb, it, nx2)) gap2 = hgap * 2 / 5;
                    contentW += gap2;
                }
            }
            int inner2 = (cl[c].colW > cl[c].regW ? cl[c].colW : cl[c].regW)
                - (real ? ((d->dirLR ? titleH : 0) + 2 * sp) : 0);
            if (inner2 < 0) inner2 = 0;
            int off = (inner2 - contentW) / 2;
            if (off < 0) off = 0;
            int cc = cl[c].regX;
            if (d->dirLR) cc += (real ? titleH + sp : 0) + off;
            else cc += (real ? sp : 0) + off;
            for (int k2 = 0; k2 < cnt2; k2++) {
                int it = items[k2];
                if (it < n) {
                    MerNode* nd = &d->nodes[it];
                    if (d->dirLR) {
                        nd->y = cc;
                        int mo2 = (rankMain[r] - nd->w) / 2;
                        if (mo2 < 0) mo2 = 0;
                        nd->x = mainBase[r] + mo2;
                    } else {
                        nd->x = cc;
                        int mo2 = (rankMain[r] - nd->h) / 2;
                        if (mo2 < 0) mo2 = 0;
                        nd->y = mainBase[r] + mo2;
                    }
                    int gap2 = hgap;
                    if (k2 + 1 < cnt2) {
                        int nx2 = items[k2 + 1];
                        if (nx2 >= n || it >= n) gap2 = hgap * 2 / 5;
                        else if (!MerLinked(nE, nea, neb, it, nx2)) gap2 = hgap * 2 / 5;
                    }
                    cc += (d->dirLR ? nd->h : nd->w) + gap2;
                } else {
                    int v = it - n;
                    if (d->dirLR) {
                        wpy[v] = cc + vw / 2;
                        wpx[v] = mainBase[r] + rankMain[r] / 2;
                    } else {
                        wpx[v] = cc + vw / 2;
                        wpy[v] = mainBase[r] + rankMain[r] / 2;
                    }
                    cc += vw + (k2 + 1 < cnt2 ? hgap * 2 / 5 : 0);
                }
            }
        }
        if (gK[c] >= 2) {
            int spanW = 0, needW = 0, gapC = hgap, cx[4];
            for (int q = 0; q < gK[c]; q++) needW += gCW[c * 4 + q];
            int innerW = (cl[c].colW > cl[c].regW ? cl[c].colW : cl[c].regW) - 2 * sp;
            if (gK[c] > 1) {
                gapC = (innerW - needW) / (gK[c] - 1);
                if (gapC > hgap) gapC = hgap;
                if (gapC < hgap / 2) gapC = hgap / 2;
            }
            spanW = needW + gapC * (gK[c] - 1);
            int curX = cl[c].regX + sp;
            if (innerW > spanW) curX += (innerW - spanW) / 2;
            for (int q = 0; q < gK[c]; q++) {
                int qq = gOrd[c * 4 + q];
                cx[qq] = curX;
                curX += gCW[c * 4 + qq] + gapC;
            }
            for (int k = 0; k < cl[c].mn; k++) {
                int ni = memb[cl[c].m0 + k];
                int q = gCol[ni];
                MerNode* nd = &d->nodes[ni];
                nd->x = cx[q] + (gCW[c * 4 + q] - nd->w) / 2;
                int mo2 = (rankMain[grank[ni]] - nd->h) / 2;
                nd->y = mainBase[grank[ni]] + (mo2 < 0 ? 0 : mo2);
            }
            for (int v = 0; v < vG; v++) {
                if (vreg[v] != c) continue;
                int e = vexG[v], r = vrkG[v];
                int ra = grank[nea[e]], rb = grank[neb[e]];
                int xa = d->nodes[nea[e]].x + d->nodes[nea[e]].w / 2;
                int xb = d->nodes[neb[e]].x + d->nodes[neb[e]].w / 2;
                int nx = xa;
                if (rb != ra) nx = xa + (xb - xa) * (r - ra) / (rb - ra);
                for (int guard = 0; guard < 40; guard++) {
                    BOOL hit = FALSE;
                    for (int k = 0; k < cl[c].mn && !hit; k++) {
                        int ni = memb[cl[c].m0 + k];
                        if (grank[ni] != r) continue;
                        MerNode* nd = &d->nodes[ni];
                        if (nx + vw / 2 > nd->x - UI_Scale(4) &&
                            nx - vw / 2 < nd->x + nd->w + UI_Scale(4)) {
                            nx = nd->x + nd->w + vw + UI_Scale(4);
                            hit = TRUE;
                        }
                    }
                    if (!hit) break;
                }
                wpx[v] = nx;
            }
            continue;
        }
        if (!d->dirLR && real && cl[c].mn > 1) {
            int wideR = -1, wideCnt = 1;
            for (int r = cl[c].rMin; r <= cl[c].rMax; r++) {
                int k2 = 0;
                for (int k = 0; k < cl[c].mn; k++)
                    if (grank[memb[cl[c].m0 + k]] == r) k2++;
                if (k2 > wideCnt) { wideCnt = k2; wideR = r; }
            }
            int nCol = 0;
            int cols[8];
            if (wideR >= 0) {
                for (int k = 0; k < cl[c].mn && nCol < 8; k++) {
                    int i = memb[cl[c].m0 + k];
                    if (grank[i] != wideR) continue;
                    cols[nCol++] = d->nodes[i].x + d->nodes[i].w / 2;
                }
                for (int p2 = 1; p2 < nCol; p2++) {
                    int cur = cols[p2], p3 = p2 - 1;
                    while (p3 >= 0 && cols[p3] > cur) { cols[p3 + 1] = cols[p3]; p3--; }
                    cols[p3 + 1] = cur;
                }
            }
            for (int r = cl[c].rMin; nCol >= 2 && r <= cl[c].rMax; r++) {
                if (r == wideR) continue;
                int rowId[8], rowNb[8], cnt2 = 0;
                for (int k = 0; k < cl[c].mn && cnt2 < 8; k++) {
                    int i = memb[cl[c].m0 + k];
                    if (grank[i] != r) continue;
                    int sum = 0, nc = 0;
                    for (int e = 0; e < nE; e++) {
                        int other = -1;
                        if (nea[e] == i) other = neb[e];
                        else if (neb[e] == i) other = nea[e];
                        if (other < 0 || grank[other] == r) continue;
                        if (cluOf[other] != c) continue;
                        sum += d->nodes[other].x + d->nodes[other].w / 2;
                        nc++;
                    }
                    rowId[cnt2] = i;
                    rowNb[cnt2] = nc ? sum / nc : d->nodes[i].x + d->nodes[i].w / 2;
                    cnt2++;
                }
                if (cnt2 < 1 || cnt2 > nCol) continue;
                int bestMask = 0, bestCost = 0x7FFFFFFF;
                for (int mask = 0; mask < (1 << nCol); mask++) {
                    int bits = 0;
                    for (int b2 = 0; b2 < nCol; b2++) if (mask & (1 << b2)) bits++;
                    if (bits != cnt2) continue;
                    int cost = 0, sel[8], ns = 0;
                    for (int b2 = 0; b2 < nCol; b2++)
                        if (mask & (1 << b2)) sel[ns++] = b2;
                    for (int k = 0; k < cnt2; k++) {
                        int d2 = rowNb[k] - cols[sel[k]];
                        cost += d2 < 0 ? -d2 : d2;
                    }
                    if (cost < bestCost) { bestCost = cost; bestMask = mask; }
                }
                int sel[8], ns = 0;
                for (int b2 = 0; b2 < nCol; b2++)
                    if (bestMask & (1 << b2)) sel[ns++] = b2;
                int nextX = -0x7FFFFFFF;
                for (int k = 0; k < cnt2; k++) {
                    MerNode* nd = &d->nodes[rowId[k]];
                    int nx = cols[sel[k]] - nd->w / 2;
                    if (nx < nextX) nx = nextX;
                    nd->x = nx;
                    nextX = nx + nd->w + hgap / 2;
                }
            }
            for (int r = cl[c].rMin; wideR >= 0 && r <= cl[c].rMax; r++) {
                if (r == wideR) continue;
                for (int v = 0; v < vG; v++) {
                    if (vreg[v] != c || vrkG[v] != r) continue;
                    int e = vexG[v], ra = grank[nea[e]], rb = grank[neb[e]];
                    int xa = d->nodes[nea[e]].x + d->nodes[nea[e]].w / 2;
                    int xb = d->nodes[neb[e]].x + d->nodes[neb[e]].w / 2;
                    int nx = xa;
                    if (rb != ra) nx = xa + (xb - xa) * (r - ra) / (rb - ra);
                    for (int guard = 0; guard < 40; guard++) {
                        BOOL hit = FALSE;
                        for (int k = 0; k < cl[c].mn && !hit; k++) {
                            MerNode* nd = &d->nodes[memb[cl[c].m0 + k]];
                            if (grank[memb[cl[c].m0 + k]] != r) continue;
                            if (nx + vw / 2 > nd->x - UI_Scale(4) &&
                                nx - vw / 2 < nd->x + nd->w + UI_Scale(4)) {
                                nx = nd->x + nd->w + vw + UI_Scale(4);
                                hit = TRUE;
                            }
                        }
                        if (!hit) break;
                    }
                    wpx[v] = nx;
                }
            }
            int bl = 0x7FFFFFFF, br = 0;
            for (int k = 0; k < cl[c].mn; k++) {
                MerNode* nd = &d->nodes[memb[cl[c].m0 + k]];
                if (nd->x < bl) bl = nd->x;
                if (nd->x + nd->w > br) br = nd->x + nd->w;
            }
            int iw = (cl[c].colW > cl[c].regW ? cl[c].colW : cl[c].regW) - 2 * sp;
            int il = cl[c].regX + sp;
            if (bl <= br && iw > 0) {
                int shift = (il + iw / 2) - (bl + br) / 2;
                if (bl + shift < il) shift = il - bl;
                if (br + shift > il + iw) shift = il + iw - br;
                if (shift) {
                    for (int k = 0; k < cl[c].mn; k++) d->nodes[memb[cl[c].m0 + k]].x += shift;
                    for (int v = 0; v < vG; v++)
                        if (vreg[v] == c) wpx[v] += shift;
                }
            }
        }
    }
    if (!d->dirLR) {
        int* des = (int*)malloc(sizeof(int) * (size_t)nNn);
        if (des) {
            for (int r = 0; r < nr; r++) {
                int only = -1, nRankClu = 0;
                for (int c = 0; c < nC; c++) {
                    if (cl[c].mn <= 0 || r < cl[c].rMin || r > cl[c].rMax) continue;
                    only = c;
                    nRankClu++;
                }
                if (nRankClu != 1 || only < 0) continue;
                int c = only;
                if (cl[c].sub < 0 || cl[c].rMin != cl[c].rMax) continue;
                int cnt2 = 0;
                for (int k = 0; k < cl[c].mn; k++) {
                    int i = memb[cl[c].m0 + k];
                    if (grank[i] != r) continue;
                    int sum = 0, nc = 0;
                    for (int e = 0; e < nE; e++) {
                        int other = -1;
                        if (nea[e] == i) other = neb[e];
                        else if (neb[e] == i) other = nea[e];
                        if (other < 0 || cluOf[other] == c || grank[other] == r) continue;
                        sum += d->nodes[other].x + d->nodes[other].w / 2;
                        nc++;
                    }
                    des[i] = nc ? sum / nc : d->nodes[i].x + d->nodes[i].w / 2;
                    items[cnt2++] = i;
                }
                if (cnt2 < 2) continue;
                for (int p2 = 1; p2 < cnt2; p2++) {
                    int cur = items[p2], p3 = p2 - 1;
                    while (p3 >= 0 && des[items[p3]] > des[cur]) {
                        items[p3 + 1] = items[p3];
                        p3--;
                    }
                    items[p3 + 1] = cur;
                }
                int oldW = d->nodes[items[cnt2 - 1]].x + d->nodes[items[cnt2 - 1]].w
                         - d->nodes[items[0]].x;
                int newW = des[items[cnt2 - 1]] - des[items[0]];
                if (newW <= oldW) continue;
                int nextX = margin;
                for (int k = 0; k < cnt2; k++) {
                    MerNode* nd = &d->nodes[items[k]];
                    int nx = des[items[k]] - nd->w / 2;
                    if (nx < nextX) nx = nextX;
                    nd->x = nx;
                    nextX = nx + nd->w + hgap;
                }
            }
            free(des);
        }
    }
    int maxX0 = 0;
    for (int i = 0; i < n; i++)
        if (d->nodes[i].x + d->nodes[i].w > maxX0) maxX0 = d->nodes[i].x + d->nodes[i].w;
    {
        int crossMax = maxX0;
        if (d->dirLR) {
            crossMax = 0;
            for (int i = 0; i < n; i++)
                if (d->nodes[i].y + d->nodes[i].h > crossMax)
                    crossMax = d->nodes[i].y + d->nodes[i].h;
        }
        int* fv = (int*)malloc(sizeof(int) * (size_t)vCap);
        int* fdes = (int*)malloc(sizeof(int) * (size_t)vCap);
        int* iv = (int*)malloc(sizeof(int) * 2 * (size_t)(nNn + vCap));
        int* miv = (int*)malloc(sizeof(int) * 2 * (size_t)(nNn + vCap));
        int* wiv = (int*)malloc(sizeof(int) * 2 * (size_t)(nNn + 2 * vCap));
        int* gsl = (int*)malloc(sizeof(int) * (size_t)(nNn + vCap + 2));
        int* gsr = (int*)malloc(sizeof(int) * (size_t)(nNn + vCap + 2));
        if (fv && fdes && iv && miv && wiv && gsl && gsr) {
            for (int v = 0; v < vG; v++) {
                if (vreg[v] >= 0) continue;
                int e = vexG[v], rr = vrkG[v];
                int ra = grank[nea[e]], rb = grank[neb[e]];
                int pa = d->dirLR
                    ? d->nodes[nea[e]].y + d->nodes[nea[e]].h / 2
                    : d->nodes[nea[e]].x + d->nodes[nea[e]].w / 2;
                int pb = d->dirLR
                    ? d->nodes[neb[e]].y + d->nodes[neb[e]].h / 2
                    : d->nodes[neb[e]].x + d->nodes[neb[e]].w / 2;
                int des = pa + (pb - pa) * (rr - ra) / (rb - ra);
                fdes[v] = des;
                if (d->dirLR) {
                    wpy[v] = des;
                    wpx[v] = mainBase[rr] + rankMain[rr] / 2;
                } else {
                    wpx[v] = des;
                    wpy[v] = mainBase[rr] + rankMain[rr] / 2;
                }
            }
            int hw = vw / 2 + UI_Scale(3);
            for (int r = 0; r < nr; r++) {
                int nFree = 0, nIv = 0;
                for (int v = 0; v < vG; v++)
                    if (vreg[v] < 0 && vrkG[v] == r) fv[nFree++] = v;
                if (nFree == 0) continue;
                for (int i = 0; i < n; i++) {
                    if (grank[i] != r) continue;
                    if (d->dirLR) {
                        iv[nIv * 2] = d->nodes[i].y;
                        iv[nIv * 2 + 1] = d->nodes[i].y + d->nodes[i].h;
                    } else {
                        iv[nIv * 2] = d->nodes[i].x;
                        iv[nIv * 2 + 1] = d->nodes[i].x + d->nodes[i].w;
                    }
                    nIv++;
                }
                for (int v = 0; v < vG; v++) {
                    if (vreg[v] < 0 || vrkG[v] != r) continue;
                    int cpos = d->dirLR ? wpy[v] : wpx[v];
                    iv[nIv * 2] = cpos - vw / 2;
                    iv[nIv * 2 + 1] = cpos + vw / 2;
                    nIv++;
                }
                for (int k = 1; k < nIv; k++) {
                    int* cur = &iv[k * 2];
                    int j2 = k - 1;
                    while (j2 >= 0 && iv[j2 * 2] > cur[0]) {
                        iv[j2 * 2 + 2] = iv[j2 * 2];
                        iv[j2 * 2 + 3] = iv[j2 * 2 + 1];
                        j2--;
                    }
                    iv[j2 * 2 + 2] = cur[0];
                    iv[j2 * 2 + 3] = cur[1];
                }
                int nMi = 0;
                for (int k = 0; k < nIv; k++) {
                    if (nMi > 0 && iv[k * 2] <= miv[nMi * 2 - 1]) {
                        if (iv[k * 2 + 1] > miv[nMi * 2 - 1])
                            miv[nMi * 2 - 1] = iv[k * 2 + 1];
                    } else {
                        miv[nMi * 2] = iv[k * 2];
                        miv[nMi * 2 + 1] = iv[k * 2 + 1];
                        nMi++;
                    }
                }
                int nGap = 0, prev = margin;
                for (int k = 0; k < nMi; k++) {
                    if (miv[k * 2] - prev >= vw) { gsl[nGap] = prev; gsr[nGap] = miv[k * 2]; nGap++; }
                    if (miv[k * 2 + 1] > prev) prev = miv[k * 2 + 1];
                }
                if (crossMax - prev >= vw) { gsl[nGap] = prev; gsr[nGap] = crossMax; nGap++; }
                for (int p2 = 1; p2 < nFree; p2++) {
                    int cur = fv[p2];
                    int kv = fdes[cur];
                    int p3 = p2 - 1;
                    while (p3 >= 0 && fdes[fv[p3]] > kv) { fv[p3 + 1] = fv[p3]; p3--; }
                    fv[p3 + 1] = cur;
                }
                int nWk = nMi;
                for (int k = 0; k < nMi; k++) { wiv[k * 2] = miv[k * 2]; wiv[k * 2 + 1] = miv[k * 2 + 1]; }
                for (int q = 0; q < nFree; q++) {
                    int v = fv[q];
                    int best = 0, bestCand = fdes[v], bestDist = 0x7FFFFFFF;
                    for (int gi = 0; gi < nGap; gi++) {
                        int lo = gsl[gi] + hw, hi = gsr[gi] - hw;
                        if (lo > hi) continue;
                        int cand = fdes[v];
                        if (cand < lo) cand = lo;
                        if (cand > hi) cand = hi;
                        if (MerIvHit(wiv, nWk, cand - hw, cand + hw)) {
                            int c2 = cand;
                            while (c2 <= hi && MerIvHit(wiv, nWk, c2 - hw, c2 + hw)) c2 += vw + UI_Scale(5);
                            int okR = (c2 <= hi) ? c2 : -1;
                            int c3 = cand;
                            while (c3 >= lo && MerIvHit(wiv, nWk, c3 - hw, c3 + hw)) c3 -= vw + UI_Scale(5);
                            int okL = (c3 >= lo) ? c3 : -1;
                            if (okR >= 0 && (okL < 0 || okR - cand <= cand - okL)) cand = okR;
                            else if (okL >= 0) cand = okL;
                            else continue;
                        }
                        int dist = cand > fdes[v] ? cand - fdes[v] : fdes[v] - cand;
                        if (dist < bestDist) { bestDist = dist; best = cand; }
                    }
                    if (bestDist < 0x7FFFFFFF) {
                        wiv[nWk * 2] = best - hw;
                        wiv[nWk * 2 + 1] = best + hw;
                        nWk++;
                        if (d->dirLR) wpy[v] = best;
                        else wpx[v] = best;
                    }
                }
            }
        }
        free(fv); free(fdes); free(iv); free(miv); free(wiv); free(gsl); free(gsr);
    }
    if (d->dirBT) {
        for (int i = 0; i < n; i++)
            d->nodes[i].y = mainTotal + margin * 2 - d->nodes[i].y - d->nodes[i].h;
        for (int v = 0; v < vG; v++)
            wpy[v] = mainTotal + margin * 2 - wpy[v];
    }
    if (d->dirLR && d->dirBT) {
        for (int i = 0; i < n; i++)
            d->nodes[i].x = maxX0 - d->nodes[i].x - d->nodes[i].w;
        for (int v = 0; v < vG; v++) wpx[v] = maxX0 - wpx[v];
    }
    for (int s = 0; s < nSub; s++) {
        int c = -1;
        for (int q = 0; q < nC; q++) if (cl[q].sub == s) { c = q; break; }
        if (c < 0) { d->subs[s].w = 0; continue; }
        if (cl[c].mn == 0) {
            int tw = d->subs[s].title[0]
                ? TextW(hdc, f->sm, d->subs[s].title) + sp * 2 : UI_Scale(60);
            d->subs[s].x = cl[c].regX;
            d->subs[s].y = margin;
            d->subs[s].w = tw > UI_Scale(60) ? tw : UI_Scale(60);
            d->subs[s].h = titleH + UI_Scale(10);
            continue;
        }
        int l = 0x7FFFFFFF, t = 0x7FFFFFFF, rgt = 0, bot = 0;
        for (int i = 0; i < n; i++) {
            MerNode* nd = &d->nodes[i];
            if (nd->sub != s) continue;
            if (nd->x < l) l = nd->x;
            if (nd->y < t) t = nd->y;
            if (nd->x + nd->w > rgt) rgt = nd->x + nd->w;
            if (nd->y + nd->h > bot) bot = nd->y + nd->h;
        }
        d->subs[s].x = l - sp;
        d->subs[s].y = t - sp - titleH;
        d->subs[s].w = (rgt + sp) - (l - sp);
        if (!d->dirLR && cl[c].colW > d->subs[s].w) {
            int left2 = cl[c].regX < l - sp ? cl[c].regX : l - sp;
            int rightA = cl[c].regX + cl[c].colW;
            int rightB = rgt + sp;
            d->subs[s].x = left2;
            d->subs[s].w = (rightA > rightB ? rightA : rightB) - left2;
        }
        d->subs[s].h = (bot + sp) - (t - sp - titleH);
    }
    for (int v = 0; v < vG; v++) {
        MerEdge* ge = &d->edges[vexG[v]];
        int* nw = (int*)realloc(ge->wps, sizeof(int) * 2 * (size_t)(ge->nWps + 1));
        if (nw) {
            ge->wps = nw;
            ge->wps[ge->nWps * 2] = wpx[v];
            ge->wps[ge->nWps * 2 + 1] = wpy[v];
            ge->nWps++;
        }
    }
    if (!d->dirLR) {
        int minX2 = 0x7FFFFFFF, maxX2 = 0;
        for (int i = 0; i < n; i++) {
            if (d->nodes[i].x < minX2) minX2 = d->nodes[i].x;
            if (d->nodes[i].x + d->nodes[i].w > maxX2) maxX2 = d->nodes[i].x + d->nodes[i].w;
        }
        if (minX2 == 0x7FFFFFFF) minX2 = margin;
        int leftN = 0, rightN = 0;
        for (int e = 0; e < nE; e++) {
            MerEdge* ed = &d->edges[e];
            if (!ed->back) { ed->side = 0; continue; }
            ed->side = ((leftN + rightN) & 1) ? -1 : +1;
            int loop = (ed->side > 0) ? rightN++ : leftN++;
            int lw = ed->label[0]
                ? TextW(hdc, f->sm, ed->label) / 2 + UI_Scale(8) : UI_Scale(6);
            int need = UI_Scale(72) + loop * UI_Scale(26) + lw;
            ed->corrX = (ed->side > 0) ? maxX2 + need : minX2 - need;
        }
        int shift = 0;
        for (int e = 0; e < nE; e++) {
            if (d->edges[e].side < 0) {
                int minXe = margin;
                if (d->edges[e].label[0])
                    minXe += TextW(hdc, f->sm, d->edges[e].label) / 2 + UI_Scale(4);
                if (minXe - d->edges[e].corrX > shift)
                    shift = minXe - d->edges[e].corrX;
            }
        }
        if (shift > 0) {
            for (int i = 0; i < n; i++) d->nodes[i].x += shift;
            for (int e = 0; e < nE; e++)
                if (d->edges[e].side) d->edges[e].corrX += shift;
            for (int s = 0; s < nSub; s++) if (d->subs[s].w) d->subs[s].x += shift;
            for (int v = 0; v < vG; v++) {
                wpx[v] += shift;
                MerEdge* ge = &d->edges[vexG[v]];
                for (int wi = 0; wi < ge->nWps; wi++) ge->wps[wi * 2] += shift;
            }
        }
    }
    int maxXf = 0, maxYf = 0;
    for (int i = 0; i < n; i++) {
        if (d->nodes[i].x + d->nodes[i].w > maxXf) maxXf = d->nodes[i].x + d->nodes[i].w;
        if (d->nodes[i].y + d->nodes[i].h > maxYf) maxYf = d->nodes[i].y + d->nodes[i].h;
    }
    for (int e = 0; e < nE; e++) {
        if (d->edges[e].side <= 0) continue;
        int right2 = d->edges[e].corrX + UI_Scale(10);
        if (d->edges[e].label[0]) {
            int lw2 = TextW(hdc, f->sm, d->edges[e].label) / 2 + UI_Scale(6);
            if (lw2 > UI_Scale(10)) right2 = d->edges[e].corrX + lw2;
        }
        if (right2 > maxXf) maxXf = right2;
    }
    for (int s = 0; s < nSub; s++) {
        if (!d->subs[s].w) continue;
        if (d->subs[s].x + d->subs[s].w > maxXf) maxXf = d->subs[s].x + d->subs[s].w;
        if (d->subs[s].y + d->subs[s].h > maxYf) maxYf = d->subs[s].y + d->subs[s].h;
    }
    for (int e = 0; e < nE; e++) {
        if (d->edges[e].label[0]) {
            maxXf += UI_Scale(4);
            maxYf += UI_Scale(4);
            break;
        }
    }
    d->sz.cx = maxXf + margin;
    d->sz.cy = maxYf + margin;
    free(nea); free(neb); free(gback); free(grank);
    free(cluOf); free(cl); free(memb); free(mfill);
    free(cea); free(ceb); free(cback); free(clvl); free(cpos); free(ckey);
    free(regOrder);
    free(wpx); free(wpy); free(vexG); free(vrkG); free(vreg);
    free(irankA); free(aea); free(aeb); free(abk);
    free(iposA); free(ikeyA);
    free(rankMain); free(mainBase); free(items); free(occ);
    free(gK); free(gCol); free(gCW); free(gOrd);
}

static void FlowLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    int n = d->nNodes;
    BOOL isState = (d->kind == 2);
    int padX = isState ? UI_Scale(13) : UI_Scale(14);
    int padY = isState ? UI_Scale(4)  : UI_Scale(7);
    int hgap = isState ? UI_Scale(40) : UI_Scale(44);
    int vgap = isState ? UI_Scale(60) : UI_Scale(52);
    int margin = UI_Scale(12);
    if (d->kind == 3 || d->kind == 4)
        ClassErFixSizeReal(d, hdc, f);
    for (int i = 0; i < n; i++) {
        MerNode* nd = &d->nodes[i];
        if (nd->wFix > 0 && nd->hFix > 0) {
            nd->w = nd->wFix;
            nd->h = nd->hFix;
            continue;
        }
        if (isState && (nd->shape == MER_START || nd->shape == MER_END)) {
            nd->w = nd->h = (nd->shape == MER_START) ? UI_Scale(12) : UI_Scale(17);
            continue;
        }
        int tw, th;
        LabelSize(hdc, f->body, nd->label, &tw, &th);
        nd->w = tw + padX * 2;
        nd->h = th + padY * 2;
        if (nd->shape == MER_CYL) nd->h += UI_Scale(13);
        if (nd->w < nd->h) nd->w = nd->h;
        if (nd->shape == MER_CIRCLE) {
            int m = nd->w > nd->h ? nd->w : nd->h;
            nd->w = nd->h = m;
        } else if (nd->shape == MER_DIAMOND) {
            nd->w = (tw + padX * 2) * 2;
            nd->h = (th + padY * 2) * 2;
        }
    }
    /* flowchart 全量交给 dagre（含无子图的简单图），与 mermaid 一致。
       g_merDagre 关掉时只有带子图的才退回老的复合布局，无子图的继续用下面的简单布局。 */
    if (d->kind == 0 && (g_merDagre || d->nSubs > 0)) {
        FlowLayoutCompound(d, hdc, f);
        return;
    }
    {
        int* head = (int*)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
        int* eit  = (int*)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
        int* nxt  = (int*)malloc((size_t)(d->nEdges > 0 ? d->nEdges : 1) * sizeof(int));
        int* mark = (int*)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
        int* stack = (int*)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
        if (head && eit && nxt && mark && stack) {
            for (int i = 0; i < n; i++) head[i] = -1;
            for (int e = 0; e < d->nEdges; e++) {
                nxt[e] = head[d->edges[e].a];
                head[d->edges[e].a] = e;
            }
            int top = 0;
            for (int s = 0; s < n; s++) {
                if (mark[s]) continue;
                mark[s] = 1; stack[top++] = s; eit[s] = head[s];
                while (top > 0) {
                    int u = stack[top - 1];
                    int e = eit[u];
                    if (e < 0) { mark[u] = 2; top--; continue; }
                    eit[u] = nxt[e];
                    int v = d->edges[e].b;
                    if (mark[v] == 1) d->edges[e].back = TRUE;
                    else if (mark[v] == 0) { mark[v] = 1; stack[top++] = v; eit[v] = head[v]; }
                }
            }
        }
        free(head); free(eit); free(nxt); free(mark); free(stack);
    }
    for (int pass = 0; pass < n; pass++) {
        BOOL changed = FALSE;
        for (int i = 0; i < d->nEdges; i++) {
            if (d->edges[i].back) continue;
            MerNode* a = &d->nodes[d->edges[i].a];
            MerNode* b = &d->nodes[d->edges[i].b];
            if (b->rank < a->rank + 1) { b->rank = a->rank + 1; changed = TRUE; }
        }
        if (!changed) break;
    }
    if (isState) {
        for (int i = 0; i < n; i++) {
            MerNode* nd = &d->nodes[i];
            if (nd->shape != MER_END) continue;
            int pred = -1, nIn = 0;
            for (int e = 0; e < d->nEdges; e++)
                if (d->edges[e].b == i && !d->edges[e].back) { pred = d->edges[e].a; nIn++; }
            if (nIn != 1 || pred < 0) continue;
            int pr = d->nodes[pred].rank;
            for (int e = 0; e < d->nEdges; e++) {
                if (d->edges[e].a != pred || d->edges[e].b == i) continue;
                MerNode* s = &d->nodes[d->edges[e].b];
                if (s->shape != MER_END && !d->edges[e].back && s->rank == pr + 1) {
                    nd->rank = s->rank;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < n; i++) d->nodes[i].layer = d->nodes[i].rank;
    int maxRank = 0;
    for (int i = 0; i < n; i++)
        if (d->nodes[i].rank > maxRank) maxRank = d->nodes[i].rank;
    int nr = maxRank + 1;
    int* cnt = (int*)calloc((size_t)nr, sizeof(int));
    int* rankOf = (int*)calloc((size_t)n, sizeof(int));
    int** buckets = (int**)calloc((size_t)nr, sizeof(int*));
    for (int i = 0; i < n; i++) cnt[d->nodes[i].rank]++;
    for (int r = 0; r < nr; r++) buckets[r] = (int*)calloc((size_t)cnt[r] ? (size_t)cnt[r] : 1, sizeof(int));
    int* fill = (int*)calloc((size_t)nr, sizeof(int));
    for (int i = 0; i < n; i++) {
        int r = d->nodes[i].rank;
        rankOf[i] = fill[r];
        buckets[r][fill[r]++] = i;
    }
    for (int sweep = 0; sweep < 4; sweep++) {
        for (int r = 1; r < nr; r++) {
            for (int k = 0; k < fill[r]; k++) {
                int i = buckets[r][k];
                double sum = 0; int c = 0;
                for (int e = 0; e < d->nEdges; e++) {
                    if (d->edges[e].b == i && d->nodes[d->edges[e].a].rank == r - 1) {
                        sum += rankOf[d->edges[e].a]; c++;
                    }
                    if (d->edges[e].a == i && d->nodes[d->edges[e].b].rank == r - 1) {
                        sum += rankOf[d->edges[e].b]; c++;
                    }
                }
                d->nodes[i].x = (c > 0) ? (int)(sum / c * 1000) : k * 1000 + k;
                if (isState && d->nodes[i].shape == MER_END) d->nodes[i].x = 0x40000000;
            }
            for (int k2 = 1; k2 < fill[r]; k2++) {
                int cur = buckets[r][k2];
                int key = d->nodes[cur].x;
                int k3 = k2 - 1;
                while (k3 >= 0 && d->nodes[buckets[r][k3]].x > key) {
                    buckets[r][k3 + 1] = buckets[r][k3];
                    k3--;
                }
                buckets[r][k3 + 1] = cur;
            }
            for (int k2 = 0; k2 < fill[r]; k2++) rankOf[buckets[r][k2]] = k2;
        }
    }
    int* rankSize = (int*)calloc((size_t)nr, sizeof(int));
    int* rankMain = (int*)calloc((size_t)nr, sizeof(int));
    for (int r = 0; r < nr; r++) {
        int w = 0;
        for (int k = 0; k < fill[r]; k++) {
            MerNode* nd = &d->nodes[buckets[r][k]];
            w += (d->dirLR ? nd->h : nd->w);
            int m = d->dirLR ? nd->w : nd->h;
            if (m > rankMain[r]) rankMain[r] = m;
        }
        w += hgap * (fill[r] > 0 ? fill[r] - 1 : 0);
        rankSize[r] = w;
    }
    int crossMax = 0, mainTotal = 0;
    for (int r = 0; r < nr; r++) {
        if (rankSize[r] > crossMax) crossMax = rankSize[r];
        mainTotal += rankMain[r] + (r + 1 < nr ? vgap : 0);
    }
    int mainPos = margin;
    for (int r = 0; r < nr; r++) {
        int cross = margin;
        for (int k = 0; k < fill[r]; k++) {
            MerNode* nd = &d->nodes[buckets[r][k]];
            int cw = d->dirLR ? nd->h : nd->w;
            int ch = d->dirLR ? nd->w : nd->h;
            int mainSize = d->dirLR ? nd->w : nd->h;
            int mainOff = (rankMain[r] - mainSize) / 2;
            int mx = mainPos + (mainOff > 0 ? mainOff : 0);
            if (d->dirLR) {
                nd->x = mx;
                nd->y = cross;
            } else {
                nd->x = cross;
                nd->y = mx;
            }
            cross += cw + hgap;
        }
        mainPos += rankMain[r] + vgap;
    }
    if (d->dirBT) {
        for (int i = 0; i < n; i++) d->nodes[i].y = mainTotal + margin * 2 - d->nodes[i].y - d->nodes[i].h;
    }
    if (d->dirLR && d->dirBT) {
        int maxX2 = 0;
        for (int i = 0; i < n; i++)
            if (d->nodes[i].x + d->nodes[i].w > maxX2) maxX2 = d->nodes[i].x + d->nodes[i].w;
        for (int i = 0; i < n; i++) d->nodes[i].x = maxX2 - d->nodes[i].x - d->nodes[i].w;
    }
    if (!d->dirLR) {
        int minX2 = 0x7FFFFFFF, maxX2 = 0;
        for (int i = 0; i < n; i++) {
            if (d->nodes[i].x < minX2) minX2 = d->nodes[i].x;
            if (d->nodes[i].x + d->nodes[i].w > maxX2) maxX2 = d->nodes[i].x + d->nodes[i].w;
        }
        if (minX2 == 0x7FFFFFFF) minX2 = margin;
        int leftN = 0, rightN = 0;
        for (int e = 0; e < d->nEdges; e++) {
            MerEdge* ed = &d->edges[e];
            if (!ed->back) { ed->side = 0; continue; }
            ed->side = ((leftN + rightN) & 1) ? -1 : +1;
            int loop = (ed->side > 0) ? rightN++ : leftN++;
            int lw = ed->label[0]
                ? TextW(hdc, f->sm, ed->label) / 2 + UI_Scale(8) : UI_Scale(6);
            int need = UI_Scale(72) + loop * UI_Scale(26) + lw;
            ed->corrX = (ed->side > 0) ? maxX2 + need : minX2 - need;
        }
        int shift = 0;
        for (int e = 0; e < d->nEdges; e++) {
            if (d->edges[e].side < 0) {
                int minXe = margin;
                if (d->edges[e].label[0])
                    minXe += TextW(hdc, f->sm, d->edges[e].label) / 2 + UI_Scale(4);
                if (minXe - d->edges[e].corrX > shift)
                    shift = minXe - d->edges[e].corrX;
            }
        }
        if (shift > 0) {
            for (int i = 0; i < n; i++) d->nodes[i].x += shift;
            for (int e = 0; e < d->nEdges; e++)
                if (d->edges[e].side) d->edges[e].corrX += shift;
        }
    }
    for (int s = 0; s < d->nSubs; s++) {
        MerSub* sub = &d->subs[s];
        int l = 0x7FFFFFFF, t = 0x7FFFFFFF, rgt = 0, bot = 0, cnt2 = 0;
        for (int i = 0; i < n; i++) {
            if (d->nodes[i].sub != s) continue;
            cnt2++;
            if (d->nodes[i].x < l) l = d->nodes[i].x;
            if (d->nodes[i].y < t) t = d->nodes[i].y;
            if (d->nodes[i].x + d->nodes[i].w > rgt) rgt = d->nodes[i].x + d->nodes[i].w;
            if (d->nodes[i].y + d->nodes[i].h > bot) bot = d->nodes[i].y + d->nodes[i].h;
        }
        if (!cnt2) { sub->w = 0; continue; }
        int sp = UI_Scale(16);
        int titleH = sub->title[0] ? UI_Scale(20) : 0;
        sub->x = l - sp; sub->y = t - sp - titleH;
        sub->w = (rgt - l) + sp * 2;
        sub->h = (bot - t) + sp * 2 + titleH;
    }
    int maxX = 0, maxY = 0;
    for (int i = 0; i < n; i++) {
        if (d->nodes[i].x + d->nodes[i].w > maxX) maxX = d->nodes[i].x + d->nodes[i].w;
        if (d->nodes[i].y + d->nodes[i].h > maxY) maxY = d->nodes[i].y + d->nodes[i].h;
    }
    for (int e = 0; e < d->nEdges; e++) {
        if (d->edges[e].side <= 0) continue;
        int right2 = d->edges[e].corrX + UI_Scale(10);
        if (d->edges[e].label[0]) {
            int lw2 = TextW(hdc, f->sm, d->edges[e].label) / 2 + UI_Scale(6);
            if (lw2 > UI_Scale(10)) right2 = d->edges[e].corrX + lw2;
        }
        if (right2 > maxX) maxX = right2;
    }
    for (int s = 0; s < d->nSubs; s++) {
        if (!d->subs[s].w) continue;
        if (d->subs[s].x + d->subs[s].w > maxX) maxX = d->subs[s].x + d->subs[s].w;
        if (d->subs[s].y + d->subs[s].h > maxY) maxY = d->subs[s].y + d->subs[s].h;
        if (d->subs[s].x < 0) {  }
    }
    for (int e = 0; e < d->nEdges; e++) {
        if (d->edges[e].label[0]) {
            maxX += UI_Scale(4);
            maxY += UI_Scale(4);
            break;
        }
    }
    d->sz.cx = maxX + margin;
    d->sz.cy = maxY + margin;
    free(cnt); free(rankOf); free(fill); free(rankSize); free(rankMain);
    for (int r = 0; r < nr; r++) free(buckets[r]);
    free(buckets);
    (void)crossMax;
}

static void SeqLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    int margin = UI_Scale(12);
    int boxH = FontH(hdc, f->bold) + UI_Scale(12);
    int gap = UI_Scale(64);
    int itemH = UI_Scale(26);
    int noteW = UI_Scale(170);
    int totalW = margin * 2;
    for (int i = 0; i < d->nParts; i++) {
        int w = TextW(hdc, f->bold, d->parts[i].name) + UI_Scale(24);
        if (w < UI_Scale(90)) w = UI_Scale(90);
        d->parts[i].x = w;
    }
    for (int i = 0; i < d->nParts; i++)
        totalW += d->parts[i].x + (i + 1 < d->nParts ? gap : 0);
    int x = margin;
    for (int i = 0; i < d->nParts; i++) {
        int w = d->parts[i].x;
        d->parts[i].x = x + w / 2;
        x += w + gap;
    }
    int y = margin + boxH + UI_Scale(34);
    int depth = 0;
    for (int i = 0; i < d->nItems; i++) {
        MerSeqItem* it = &d->items[i];
        switch (it->type) {
            case MSI_MSG:
                it->y = y;
                it->h = itemH;
                y += itemH;
                break;
            case MSI_NOTE: {
                RECT rc = { 0, 0, noteW - UI_Scale(12), 0 };
                HFONT of = (HFONT)SelectObject(hdc, f->sm);
                DrawTextW(hdc, it->text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
                SelectObject(hdc, of);
                it->y = y;
                it->h = (rc.bottom - rc.top) + UI_Scale(12);
                y += it->h + UI_Scale(4);
                break;
            }
            case MSI_FRAME_BEGIN:
                it->y = y;
                it->h = UI_Scale(14);
                y += it->h;
                depth++;
                break;
            case MSI_FRAME_ELSE:
                it->y = y;
                it->h = UI_Scale(18);
                y += it->h;
                break;
            case MSI_FRAME_END:
                it->y = y + UI_Scale(10);
                it->h = UI_Scale(14);
                y = it->y + it->h;
                if (depth > 0) depth--;
                break;
        }
    }
    if (depth > 0) y += UI_Scale(24);
    d->sz.cx = totalW;
    d->sz.cy = y + margin;
}
static void DrawArrowHead(HDC hdc, COLORREF clr, BOOL filled, int x, int y, double dx, double dy) {
    double len = sqrt(dx * dx + dy * dy);
    float xy[6];
    if (len < 0.001) return;
    dx /= len; dy /= len;
    double px = -dy, py = dx;
    int size = UI_Scale(10), hw = UI_Scale(4);
    xy[0] = (float)x;
    xy[1] = (float)y;
    xy[2] = (float)(x - size * dx + hw * px);
    xy[3] = (float)(y - size * dy + hw * py);
    xy[4] = (float)(x - size * dx - hw * px);
    xy[5] = (float)(y - size * dy - hw * py);
    /* 箭头同样走 GDI+ 抗锯齿，否则斜边会和抗锯齿的连线明显不搭 */
    if (MerAAPolygon(hdc, xy, 3, clr, filled)) return;
    {
        POINT pts[3];
        HPEN pen = MerPenC(PS_SOLID, 1, clr);
        HBRUSH br = filled ? MerBrushC(clr) : (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        pts[0].x = (LONG)xy[0]; pts[0].y = (LONG)xy[1];
        pts[1].x = (LONG)xy[2]; pts[1].y = (LONG)xy[3];
        pts[2].x = (LONG)xy[4]; pts[2].y = (LONG)xy[5];
        Polygon(hdc, pts, 3);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
    }
}
static void CurveSeg(HDC hdc, int ax, int ay, int bx, int by, int sx, int sy, int tx, int ty) {
    double L = sqrt((double)(bx - ax) * (bx - ax) + (double)(by - ay) * (by - ay));
    if (L < 1.5) return;
    double o1x = (bx - sx) / 6.0, o1y = (by - sy) / 6.0;
    double o2x = (ax - tx) / 6.0, o2y = (ay - ty) / 6.0;
    double lim = L * 0.42;
    double n1 = sqrt(o1x * o1x + o1y * o1y);
    if (n1 > lim && n1 > 0.001) { o1x *= lim / n1; o1y *= lim / n1; }
    double n2 = sqrt(o2x * o2x + o2y * o2y);
    if (n2 > lim && n2 > 0.001) { o2x *= lim / n2; o2y *= lim / n2; }
    POINT bz[4];
    bz[0].x = ax; bz[0].y = ay;
    bz[1].x = (LONG)(ax + o1x); bz[1].y = (LONG)(ay + o1y);
    bz[2].x = (LONG)(bx + o2x); bz[2].y = (LONG)(by + o2y);
    bz[3].x = bx; bz[3].y = by;
    if (!MerAABezierDC(hdc, bz, 4)) PolyBezier(hdc, bz, 4);
}
/* 抗锯齿单段直线。GDI 的 LineTo 对斜线不做抗锯齿，一眼就是阶梯。 */
static void MerLineAA(HDC hdc, int x1, int y1, int x2, int y2) {
    POINT seg[2];
    seg[0].x = x1;
    seg[0].y = y1;
    seg[1].x = x2;
    seg[1].y = y2;
    if (!MerAAPolylineDC(hdc, seg, 2)) {
        MerLineAA(hdc, x1, y1, x2, y2);
    }
}
/* 折线 + 转角圆角，离散成亚像素点列，交给抗锯齿绘制。
   dagre 的点列是给折线用的：一条边常是「一段大斜线接一段长竖线」。若用 Catmull-Rom
   平滑（CurveSeg），竖线段起点处的切线由前一个很远的折点决定，控制点被往回拽十几像素，
   画出来就是弯的。这里只在真正的转角倒一个小圆角，其余线段一律走直线。 */
static int PolyPathPoints(const int (*p)[2], int n, int rad, float* xy, int maxPts) {
    int k, cnt = 0;
    if (n <= 0 || maxPts < 2) return 0;
    xy[0] = (float)p[0][0];
    xy[1] = (float)p[0][1];
    cnt = 1;
    for (k = 1; k + 1 < n; k++) {
        int px = p[k][0], py = p[k][1];
        int ax = p[k - 1][0], ay = p[k - 1][1];
        int bx = p[k + 1][0], by = p[k + 1][1];
        int d1x = px - ax, d1y = py - ay;
        int d2x = bx - px, d2y = by - py;
        double d1 = sqrt((double)d1x * d1x + (double)d1y * d1y);
        double d2 = sqrt((double)d2x * d2x + (double)d2y * d2y);
        double cr, r, u1x, u1y, u2x, u2y, q1x, q1y, q2x, q2y;
        int j;
        if (cnt + 8 >= maxPts) break;
        if (d1 < 1.0 || d2 < 1.0) {
            xy[cnt * 2] = (float)px;
            xy[cnt * 2 + 1] = (float)py;
            cnt++;
            continue;
        }
        cr = (double)d1x * d2y - (double)d1y * d2x;
        /* 几乎共线（转角小于约 1.5°）或掉头，就不倒角，保证长直段严格直 */
        if ((double)d1x * d2x + (double)d1y * d2y <= 0 ||
            (cr < 0.026 * d1 * d2 && -cr < 0.026 * d1 * d2)) {
            xy[cnt * 2] = (float)px;
            xy[cnt * 2 + 1] = (float)py;
            cnt++;
            continue;
        }
        r = (double)rad;
        if (r > d1 * 0.5) r = d1 * 0.5;
        if (r > d2 * 0.5) r = d2 * 0.5;
        u1x = (double)-d1x / d1;
        u1y = (double)-d1y / d1;
        u2x = (double)d2x / d2;
        u2y = (double)d2y / d2;
        q1x = px + u1x * r;
        q1y = py + u1y * r;
        q2x = px + u2x * r;
        q2y = py + u2y * r;
        /* 二次贝塞尔（控制点就是转角）近似圆弧，离散成几段保持亚像素精度 */
        for (j = 0; j <= 4; j++) {
            double t = (double)j / 4.0, mt = 1.0 - t;
            xy[cnt * 2] = (float)(mt * mt * q1x + 2.0 * mt * t * px + t * t * q2x);
            xy[cnt * 2 + 1] = (float)(mt * mt * q1y + 2.0 * mt * t * py + t * t * q2y);
            cnt++;
        }
    }
    if (cnt < maxPts) {
        xy[cnt * 2] = (float)p[n - 1][0];
        xy[cnt * 2 + 1] = (float)p[n - 1][1];
        cnt++;
    }
    return cnt;
}
static int MerPushPt(int (*pts)[2], int np, int x, int y) {
    if (np > 0 && pts[np - 1][0] == x && pts[np - 1][1] == y) return np;
    if (np >= 23) return np;
    pts[np][0] = x;
    pts[np][1] = y;
    return np + 1;
}
/* 正交折线：直线段 + 拐角小圆角。不能用 Catmull-Rom 过点平滑，
 * 它会在直角处按「下一点-上一点」反向过冲，画出一串回勾。 */
static void MerOrthoPath(HDC hdc, int (*pts)[2], int np, int r) {
    if (np < 2) return;
    MoveToEx(hdc, pts[0][0], pts[0][1], NULL);
    for (int i = 1; i + 1 < np; i++) {
        int px = pts[i - 1][0], py = pts[i - 1][1];
        int cx = pts[i][0], cy = pts[i][1];
        int nx = pts[i + 1][0], ny = pts[i + 1][1];
        int l1 = (cx > px ? cx - px : px - cx) + (cy > py ? cy - py : py - cy);
        int l2 = (nx > cx ? nx - cx : cx - nx) + (ny > cy ? ny - cy : cy - ny);
        int rr = r;
        if (rr > l1 / 2) rr = l1 / 2;
        if (rr > l2 / 2) rr = l2 / 2;
        int ax = cx, ay = cy, bx = cx, by = cy;
        if (l1 > 0) { ax = cx + (px - cx) * rr / l1; ay = cy + (py - cy) * rr / l1; }
        if (l2 > 0) { bx = cx + (nx - cx) * rr / l2; by = cy + (ny - cy) * rr / l2; }
        LineTo(hdc, ax, ay);
        if (rr > 0) {
            POINT bz[4] = { { ax, ay }, { cx, cy }, { cx, cy }, { bx, by } };
            if (!MerAABezierDC(hdc, bz, 4)) PolyBezier(hdc, bz, 4);
        }
        MoveToEx(hdc, bx, by, NULL);
    }
    LineTo(hdc, pts[np - 1][0], pts[np - 1][1]);
}
static void EdgeClip(int* px, int* py, int cx, int cy, int w, int h, double fx, double fy) {
    double dx = fx, dy = fy;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 0.001) return;
    dx /= len; dy /= len;
    double tx = (w / 2.0) , ty = (h / 2.0);
    double t1 = (fabs(dx) > 0.001) ? tx / fabs(dx) : 1e9;
    double t2 = (fabs(dy) > 0.001) ? ty / fabs(dy) : 1e9;
    double t = t1 < t2 ? t1 : t2;
    *px = cx + (int)(dx * t);
    *py = cy + (int)(dy * t);
}
static void FlowDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    SetBkMode(hdc, TRANSPARENT);
    HPEN dashPen = CreatePen(PS_DASH, 1, th->frame);
    HBRUSH subBr = CreateSolidBrush(th->tableHeadBg);
    for (int s = 0; s < d->nSubs; s++) {
        MerSub* sub = &d->subs[s];
        if (!sub->w) continue;
        RECT rc = { ox + sub->x, oy + sub->y, ox + sub->x + sub->w, oy + sub->y + sub->h };
        HBRUSH ob = (HBRUSH)SelectObject(hdc, subBr);
        HPEN op = (HPEN)SelectObject(hdc, dashPen);
        if (!MerAARoundRectDC(hdc, rc.left, rc.top, rc.right, rc.bottom, UI_Scale(8), UI_Scale(8))) RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, UI_Scale(8), UI_Scale(8));
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        if (sub->title[0]) {
            SetTextColor(hdc, th->fgMuted);
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            TextOutW(hdc, rc.left + UI_Scale(8), rc.top + UI_Scale(4), sub->title, (int)wcslen(sub->title));
            SelectObject(hdc, of);
        }
    }
    DeleteObject(dashPen);
    DeleteObject(subBr);
    HPEN penSolid = CreatePen(PS_SOLID, 1, th->merEdge);
    HPEN penDash  = CreatePen(PS_DASH, 1, th->merEdge);
    HPEN penThick = CreatePen(PS_SOLID, 2, th->merEdge);
    RECT chipUsed[80];
    int nChipUsed = 0;
    int nNod = d->nNodes > 0 ? d->nNodes : 1;
    int nEdg = d->nEdges > 0 ? d->nEdges : 1;
    int* lblE = (int*)malloc(sizeof(int) * (size_t)nEdg);
    RECT* lblR = (RECT*)malloc(sizeof(RECT) * (size_t)nEdg);
    int nLbl = 0;
    int* inTopCnt = (int*)calloc((size_t)nNod, sizeof(int));
    int* outTopCnt = (int*)calloc((size_t)nNod, sizeof(int));
    int* inSlot = (int*)calloc((size_t)nEdg, sizeof(int));
    int* outSlot = (int*)calloc((size_t)nEdg, sizeof(int));
    int* eList = (int*)malloc(sizeof(int) * (size_t)nEdg);
    /* 每一层的上下边界：跨层连线改走层间缝隙，需要知道缝在哪 */
    int maxRk = 0;
    for (int i = 0; i < d->nNodes; i++)
        if (d->nodes[i].rank > maxRk) maxRk = d->nodes[i].rank;
    int* rTop = NULL;
    int* rBot = NULL;
    if (!d->dirLR && d->nSubs > 0 && d->nNodes > 0 && maxRk > 0) {
        rTop = (int*)malloc(sizeof(int) * (size_t)(maxRk + 1));
        rBot = (int*)malloc(sizeof(int) * (size_t)(maxRk + 1));
        if (rTop && rBot) {
            for (int r = 0; r <= maxRk; r++) { rTop[r] = 0x7FFFFFFF; rBot[r] = -0x7FFFFFFF; }
            for (int i = 0; i < d->nNodes; i++) {
                MerNode* nd = &d->nodes[i];
                int r = nd->rank;
                if (r < 0 || r > maxRk) continue;
                if (nd->y < rTop[r]) rTop[r] = nd->y;
                if (nd->y + nd->h > rBot[r]) rBot[r] = nd->y + nd->h;
            }
            for (int r = 0; r <= maxRk; r++)
                if (rTop[r] >= rBot[r]) { rTop[r] = 0; rBot[r] = 0; }
        }
    }
    if (inTopCnt && outTopCnt && inSlot && outSlot && eList) {
        for (int e = 0; e < d->nEdges; e++) {
            MerEdge* ee = &d->edges[e];
            MerNode* aa = &d->nodes[ee->a];
            MerNode* bb = &d->nodes[ee->b];
            if (d->dirLR || ee->side || ee->a == ee->b) continue;
            if (bb->layer == aa->layer && bb->rank == aa->rank) continue;
            inTopCnt[ee->b]++;
            outTopCnt[ee->a]++;
        }
        for (int i = 0; i < d->nNodes; i++) {
            if (inTopCnt[i] > 1) {
                int m2 = 0;
                for (int e = 0; e < d->nEdges; e++) {
                    MerEdge* ee = &d->edges[e];
                    MerNode* aa = &d->nodes[ee->a];
                    MerNode* bb = &d->nodes[ee->b];
                    if (d->dirLR || ee->side || ee->a == ee->b) continue;
                    if (bb->layer == aa->layer && bb->rank == aa->rank) continue;
                    if (ee->b == i) eList[m2++] = e;
                }
                for (int p2 = 1; p2 < m2; p2++) {
                    int cur = eList[p2];
                    int key2 = d->nodes[d->edges[cur].a].x + d->nodes[d->edges[cur].a].w / 2;
                    int p3 = p2 - 1;
                    while (p3 >= 0 && d->nodes[d->edges[eList[p3]].a].x +
                                       d->nodes[d->edges[eList[p3]].a].w / 2 > key2) {
                        eList[p3 + 1] = eList[p3];
                        p3--;
                    }
                    eList[p3 + 1] = cur;
                }
                for (int p2 = 0; p2 < m2; p2++) inSlot[eList[p2]] = p2;
            }
            if (outTopCnt[i] > 1) {
                int m2 = 0;
                for (int e = 0; e < d->nEdges; e++) {
                    MerEdge* ee = &d->edges[e];
                    MerNode* aa = &d->nodes[ee->a];
                    MerNode* bb = &d->nodes[ee->b];
                    if (d->dirLR || ee->side || ee->a == ee->b) continue;
                    if (bb->layer == aa->layer && bb->rank == aa->rank) continue;
                    if (ee->a == i) eList[m2++] = e;
                }
                for (int p2 = 1; p2 < m2; p2++) {
                    int cur = eList[p2];
                    int key2 = d->nodes[d->edges[cur].b].x + d->nodes[d->edges[cur].b].w / 2;
                    int p3 = p2 - 1;
                    while (p3 >= 0 && d->nodes[d->edges[eList[p3]].b].x +
                                       d->nodes[d->edges[eList[p3]].b].w / 2 > key2) {
                        eList[p3 + 1] = eList[p3];
                        p3--;
                    }
                    eList[p3 + 1] = cur;
                }
                for (int p2 = 0; p2 < m2; p2++) outSlot[eList[p2]] = p2;
            }
        }
    }
    int bStem = UI_Scale(30);
    int* bTgt = (int*)calloc((size_t)nNod, sizeof(int));
    int* bCnt = (int*)calloc((size_t)nNod, sizeof(int));
    int* bSlot = (int*)calloc((size_t)nEdg, sizeof(int));
    int* bOn = (int*)calloc((size_t)nEdg, sizeof(int));
    if (bTgt && bCnt && bSlot && bOn && !d->dirLR) {
        for (int e = 0; e < d->nEdges; e++) {
            MerEdge* ee = &d->edges[e];
            MerNode* aa = &d->nodes[ee->a];
            MerNode* bb = &d->nodes[ee->b];
            if (ee->side || ee->a == ee->b) continue;
            if (bb->layer == aa->layer && bb->rank == aa->rank) continue;
            if (aa->y + aa->h > bb->y) continue;
            bCnt[ee->b]++;
        }
        for (int i = 0; i < d->nNodes; i++)
            if (bCnt[i] >= 4) bTgt[i] = 1;
        for (int i = 0; i < d->nNodes; i++) {
            if (!bTgt[i]) continue;
            int m2 = 0;
            for (int e = 0; e < d->nEdges; e++) {
                MerEdge* ee = &d->edges[e];
                MerNode* aa = &d->nodes[ee->a];
                MerNode* bb = &d->nodes[ee->b];
                if (ee->b != i || ee->side || ee->a == ee->b) continue;
                if (bb->layer == aa->layer && bb->rank == aa->rank) continue;
                if (aa->y + aa->h > bb->y) continue;
                bOn[e] = 1;
                eList[m2++] = e;
            }
            for (int p2 = 1; p2 < m2; p2++) {
                int cur = eList[p2];
                int key2 = d->nodes[d->edges[cur].a].x + d->nodes[d->edges[cur].a].w / 2;
                int p3 = p2 - 1;
                while (p3 >= 0 && d->nodes[d->edges[eList[p3]].a].x +
                                   d->nodes[d->edges[eList[p3]].a].w / 2 > key2) {
                    eList[p3 + 1] = eList[p3];
                    p3--;
                }
                eList[p3 + 1] = cur;
            }
            for (int p2 = 0; p2 < m2; p2++) bSlot[eList[p2]] = p2;
        }
    }
    POINT voOld;
    SetViewportOrgEx(hdc, ox, oy, &voOld);
    /* dagre 布局下连线直接用布局给出的折点，不再套用手工通道规则 */
    int useDagre = (g_merDagre && d->kind == 0);
    for (int e = 0; e < d->nEdges; e++) {
        MerEdge* ed = &d->edges[e];
        MerNode* a = &d->nodes[ed->a];
        MerNode* b = &d->nodes[ed->b];
        HPEN pen = ed->thick ? penThick : (ed->dash ? penDash : penSolid);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        int ax = a->x + a->w / 2, ay = a->y + a->h / 2;
        int bx = b->x + b->w / 2, by = b->y + b->h / 2;
        double fx = (double)(bx - ax), fy = (double)(by - ay);
        int x1 = ax, y1 = ay, x2 = bx, y2 = by;
        double adx = 0, ady = 0;
        int lbx = -1, lby = -1;
        double shrinkA = 1.0, shrinkB = 1.0;
        if (a->shape == MER_DIAMOND) shrinkA = 0.5;
        if (b->shape == MER_DIAMOND) shrinkB = 0.5;
        if (a->shape == MER_CIRCLE) shrinkA = 0.707;
        if (b->shape == MER_CIRCLE) shrinkB = 0.707;
        {
            int px = ax, py = ay;
            EdgeClip(&px, &py, ax, ay, (int)(a->w * shrinkA), (int)(a->h * shrinkA), fx, fy);
            x1 = px; y1 = py;
            int qx = bx, qy = by;
            EdgeClip(&qx, &qy, bx, by, (int)(b->w * shrinkB), (int)(b->h * shrinkB), -fx, -fy);
            x2 = qx; y2 = qy;
        }
        if (useDagre && ed->a != ed->b) {
            /* dagre 走线：按 normalize 打断出来的 dummy 折点串连一条平滑折线，
               起止点各自朝相邻折点方向裁到节点框上（等价 assignNodeIntersects） */
            int pp[66][2];
            int np = 0, k;
            int acx = a->x + a->w / 2, acy = a->y + a->h / 2;
            int bcx = b->x + b->w / 2, bcy = b->y + b->h / 2;
            for (k = 0; k < ed->nWps && np < 64; k++) {
                pp[np][0] = ed->wps[k * 2];
                pp[np][1] = ed->wps[k * 2 + 1];
                np++;
            }
            {
                double f0x = (np > 0) ? (double)(pp[0][0] - acx) : (double)(bcx - acx);
                double f0y = (np > 0) ? (double)(pp[0][1] - acy) : (double)(bcy - acy);
                double sa = 1.0;
                if (a->shape == MER_DIAMOND) sa = 0.5;
                if (a->shape == MER_CIRCLE) sa = 0.707;
                x1 = acx; y1 = acy;
                EdgeClip(&x1, &y1, acx, acy, (int)(a->w * sa), (int)(a->h * sa), f0x, f0y);
            }
            {
                /* 终点：从 b 中心朝最后一个折点的方向裁到框上 */
                double f1x = (np > 0) ? (double)(pp[np - 1][0] - bcx) : (double)(acx - bcx);
                double f1y = (np > 0) ? (double)(pp[np - 1][1] - bcy) : (double)(acy - bcy);
                double sb = 1.0;
                if (b->shape == MER_DIAMOND) sb = 0.5;
                if (b->shape == MER_CIRCLE) sb = 0.707;
                x2 = bcx; y2 = bcy;
                EdgeClip(&x2, &y2, bcx, bcy, (int)(b->w * sb), (int)(b->h * sb), f1x, f1y);
            }
            for (k = np; k > 0; k--) {
                pp[k][0] = pp[k - 1][0];
                pp[k][1] = pp[k - 1][1];
            }
            pp[0][0] = x1; pp[0][1] = y1;
            pp[np + 1][0] = x2; pp[np + 1][1] = y2;
            np += 2;
            {
                /* 折线 + 转角圆角，用 GDI+ 抗锯齿画：GDI 的 LineTo 斜线全是阶梯 */
                float xy[1024];
                int cnt = PolyPathPoints(pp, np, UI_Scale(5), xy, 512);
                if (cnt >= 2 &&
                    !MerAAPolyline(hdc, xy, cnt, th->merEdge, ed->thick ? 2 : 1, ed->dash)) {
                    int q;
                    MoveToEx(hdc, pp[0][0], pp[0][1], NULL);
                    for (q = 1; q < np; q++) LineTo(hdc, pp[q][0], pp[q][1]);
                }
            }
            {
                double dx2 = (double)(x2 - pp[np - 2][0]);
                double dy2 = (double)(y2 - pp[np - 2][1]);
                double ln2 = sqrt(dx2 * dx2 + dy2 * dy2);
                if (ln2 > 0.001) { adx = dx2 / ln2; ady = dy2 / ln2; }
            }
            {
                int mi = np / 2;
                if (mi < 1) mi = 1;
                if (mi > np - 1) mi = np - 1;
                lbx = (pp[mi - 1][0] + pp[mi][0]) / 2;
                lby = (pp[mi - 1][1] + pp[mi][1]) / 2;
            }
        } else if (ed->a == ed->b) {
            int top = a->y;
            int cx = a->x + a->w / 2;
            POINT pts[4] = {
                { cx - UI_Scale(8), top }, { cx - UI_Scale(8), top - UI_Scale(18) },
                { cx + UI_Scale(8), top - UI_Scale(18) }, { cx + UI_Scale(8), top }
            };
            if (!MerAAPolylineDC(hdc, pts, 4)) Polyline(hdc, pts, 4);
            if (ed->arrow)
                DrawArrowHead(hdc, th->merEdge, TRUE, cx + UI_Scale(8), top, 0, 1);
            lbx = cx; lby = top - UI_Scale(18);
        } else if (!d->dirLR) {
            if (ed->side) {
                BOOL localSide = (a->sub >= 0 && a->sub == b->sub &&
                                  d->subs[a->sub].w > 0);
                int corr = ed->corrX;
                int sideDir = ed->side;
                if (localSide) {
                    int rr = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
                    int lim = d->subs[a->sub].x + d->subs[a->sub].w - UI_Scale(6);
                    corr = rr + UI_Scale(20);
                    if (corr > lim) corr = lim;
                    sideDir = 1;
                }
                int ex = (sideDir > 0) ? a->x + a->w : a->x;
                int ey = a->y + a->h / 2;
                int nx2 = (sideDir > 0) ? b->x + b->w : b->x;
                int ny2 = b->y + b->h / 2;
                POINT bz[4] = { {ex, ey}, {corr, ey}, {corr, ny2}, {nx2, ny2} };
                if (!MerAABezierDC(hdc, bz, 4)) PolyBezier(hdc, bz, 4);
                x1 = ex; y1 = ey; x2 = nx2; y2 = ny2;
                adx = (sideDir > 0) ? -1.0 : 1.0; ady = 0;
                lbx = localSide ? corr - UI_Scale(16) : corr;
                lby = (ey + ny2) / 2;
            } else if (b->layer == a->layer && b->rank == a->rank) {
                MerNode* Ln = a; MerNode* Rn = b;
                if (Ln->x > Rn->x) { Ln = b; Rn = a; }
                x1 = Ln->x + Ln->w; y1 = Ln->y + Ln->h / 2;
                x2 = Rn->x;        y2 = Rn->y + Rn->h / 2;
                MerLineAA(hdc, x1, y1, x2, y2);
                adx = (Rn == b) ? 1.0 : -1.0; ady = 0;
                lbx = (x1 + x2) / 2; lby = y1;
            } else {
                x1 = a->x + a->w / 2; y1 = a->y + a->h;
                x2 = b->x + b->w / 2; y2 = b->y;
                if (d->kind == 2) {
                    BOOL endSide = (b->shape == MER_END &&
                                    abs((b->x + b->w / 2) - x1) > UI_Scale(24));
                    if (endSide) {
                        BOOL toRight = (b->x + b->w / 2) > x1;
                        x2 = toRight ? b->x : b->x + b->w;
                        y2 = b->y + b->h / 2;
                        int dd = y2 - y1;
                        if (dd > UI_Scale(36)) dd = UI_Scale(36);
                        if (dd < UI_Scale(24)) dd = UI_Scale(24);
                        POINT bz[4] = { {x1, y1}, {x1, y1 + dd},
                                        {x2 + (toRight ? -dd : dd), y2}, {x2, y2} };
                        if (!MerAABezierDC(hdc, bz, 4)) PolyBezier(hdc, bz, 4);
                        adx = toRight ? 1.0 : -1.0; ady = 0;
                        lbx = (x1 + x2) / 2; lby = y2 - dd / 2;
                    } else {
                        if (y2 < y1 + UI_Scale(4)) y2 = y1 + UI_Scale(4);
                        int ym = (y1 + y2) / 2;
                        POINT bz[4] = { {x1, y1}, {x1, ym}, {x2, ym}, {x2, y2} };
                        if (!MerAABezierDC(hdc, bz, 4)) PolyBezier(hdc, bz, 4);
                        adx = 0; ady = 1;
                        lbx = (x1 + x2) / 2; lby = ym;
                    }
                } else {
                    if (y2 < y1 + UI_Scale(4)) y2 = y1 + UI_Scale(4);
                    int bnd = (bOn && bOn[e]);
                    if (outTopCnt && outTopCnt[ed->a] > 1) {
                        int m2 = outTopCnt[ed->a];
                        int lo = a->x + a->w * 15 / 100;
                        int span = a->w * 70 / 100;
                        x1 = lo + span * (2 * outSlot[e] + 1) / (2 * m2);
                    }
                    if (bnd) {
                        int m2 = bCnt[ed->b] > 0 ? bCnt[ed->b] : 1;
                        int cx = b->x + b->w / 2;
                        x2 = cx + (bSlot[e] - (m2 - 1) / 2) * UI_Scale(5);
                        y2 = b->y - bStem;
                    } else if (inTopCnt && inTopCnt[ed->b] > 1) {
                        int m2 = inTopCnt[ed->b];
                        int lo = b->x + b->w * 15 / 100;
                        int span = b->w * 70 / 100;
                        x2 = lo + span * (2 * inSlot[e] + 1) / (2 * m2);
                    }
                    int np = 0;
                    int pts[24][2];
                    pts[np][0] = x1; pts[np][1] = y1; np++;
                    BOOL detour = (rTop && rBot && ed->nWps > 0 &&
                                   b->rank - a->rank >= 2 && maxRk > 0);
                    if (detour) {
                        /* 跨多层：在每个层间缝隙里横向挪到通道，纵向段只穿一层，
                         * 避免长斜线从中间几层的节点框上压过去。中间各层的通道
                         * 合并成一条（取中位 x），只保留进、出两次横移，避免一串小台阶。 */
                        int cxs[24], ncx = 0, gyFirst = -1, curx = x1, cury = y1;
                        for (int wi = 0; wi < ed->nWps; wi++) {
                            int wx = ed->wps[wi * 2], wy = ed->wps[wi * 2 + 1];
                            int wr = 0, bestd = 0x7FFFFFFF;
                            for (int r = 0; r <= maxRk; r++) {
                                if (rTop[r] >= rBot[r]) continue;
                                int cy = (rTop[r] + rBot[r]) / 2;
                                int dd = cy > wy ? cy - wy : wy - cy;
                                if (dd < bestd) { bestd = dd; wr = r; }
                            }
                            int gy = wr > 0 ? (rBot[wr - 1] + rTop[wr]) / 2 : wy;
                            if (gy <= y1 + UI_Scale(2)) continue;
                            if (gyFirst < 0) gyFirst = gy;
                            if (ncx < 24) cxs[ncx++] = wx;
                        }
                        int xmid = x1;
                        if (ncx > 0) {
                            for (int p2 = 1; p2 < ncx; p2++) {
                                int cur = cxs[p2], p3 = p2 - 1;
                                while (p3 >= 0 && cxs[p3] > cur) { cxs[p3 + 1] = cxs[p3]; p3--; }
                                cxs[p3 + 1] = cur;
                            }
                            xmid = cxs[ncx / 2];
                        }
                        int gy2 = cury;
                        if (b->rank > 0 && b->rank <= maxRk &&
                            rTop[b->rank] > rBot[b->rank - 1])
                            gy2 = (rBot[b->rank - 1] + rTop[b->rank]) / 2;
                        if (gy2 < y1 + UI_Scale(4)) gy2 = y1 + UI_Scale(4);
                        if (gyFirst < 0 || gyFirst >= gy2) gyFirst = y1;
                        if (gyFirst > y1 + UI_Scale(2) &&
                            (xmid > x1 + UI_Scale(10) || xmid < x1 - UI_Scale(10))) {
                            np = MerPushPt(pts, np, x1, gyFirst);
                            np = MerPushPt(pts, np, xmid, gyFirst);
                            curx = xmid;
                            cury = gyFirst;
                        }
                        if (gy2 > cury + UI_Scale(2)) {
                            np = MerPushPt(pts, np, curx, gy2);
                            np = MerPushPt(pts, np, x2, gy2);
                        }
                    } else {
                        for (int wi = 0; wi < ed->nWps && np < 23; wi++) {
                            pts[np][0] = ed->wps[wi * 2];
                            pts[np][1] = ed->wps[wi * 2 + 1];
                            np++;
                        }
                    }
                    pts[np][0] = x2; pts[np][1] = y2; np++;
                    if (detour && np > 2) {
                        lbx = (pts[0][0] + pts[2][0]) / 2;
                        lby = pts[1][1];
                        MerOrthoPath(hdc, pts, np, UI_Scale(12));
                    } else {
                        if (ed->nWps > 0) {
                            lbx = (x1 + ed->wps[0]) / 2;
                            lby = (y1 + ed->wps[1]) / 2;
                        } else {
                            lbx = (x1 + x2) / 2; lby = (y1 + y2) / 2;
                        }
                        for (int pi = 0; pi + 1 < np; pi++) {
                            int ax3 = pts[pi][0], ay3 = pts[pi][1];
                            int bx3 = pts[pi + 1][0], by3 = pts[pi + 1][1];
                            int sx3 = pi > 0 ? pts[pi - 1][0] : ax3;
                            int sy3 = pi > 0 ? pts[pi - 1][1] : ay3 - UI_Scale(10);
                            int tx3 = pi + 2 < np ? pts[pi + 2][0] : bx3;
                            int ty3 = pi + 2 < np ? pts[pi + 2][1] : by3 + UI_Scale(10);
                            CurveSeg(hdc, ax3, ay3, bx3, by3, sx3, sy3, tx3, ty3);
                        }
                    }
                    adx = 0; ady = 1;
                }
            }
        } else {
            MerLineAA(hdc, x1, y1, x2, y2);
            {
                double ln2 = sqrt(fx * fx + fy * fy);
                if (ln2 > 0.001) { adx = fx / ln2; ady = fy / ln2; }
            }
            lbx = (x1 + x2) / 2; lby = (y1 + y2) / 2;
        }
        if (useDagre || !(bOn && bOn[e])) {
            int mx = ed->markA ? x1 : x2, my = ed->markA ? y1 : y2;
            double mdx = ed->markA ? -adx : adx, mdy = ed->markA ? -ady : ady;
            switch (ed->rel) {
                case MRE_INHERIT: {
                    double px = -mdy, py = mdx;
                    POINT tri[4];
                    tri[0].x = mx; tri[0].y = my;
                    tri[1].x = (int)(mx - UI_Scale(14) * mdx + UI_Scale(7) * px);
                    tri[1].y = (int)(my - UI_Scale(14) * mdy + UI_Scale(7) * py);
                    tri[2].x = (int)(mx - UI_Scale(14) * mdx - UI_Scale(7) * px);
                    tri[2].y = (int)(my - UI_Scale(14) * mdy - UI_Scale(7) * py);
                    tri[3] = tri[0];
                    HBRUSH ob3 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    if (!MerAAPolygonDC(hdc, tri, 4)) Polygon(hdc, tri, 4);
                    SelectObject(hdc, ob3);
                    break;
                }
                case MRE_COMPOSE:
                case MRE_AGGREG: {
                    double px = -mdy, py = mdx;
                    int L = UI_Scale(18), W2 = UI_Scale(6);
                    POINT dm[5];
                    dm[0].x = mx; dm[0].y = my;
                    dm[1].x = (int)(mx - L / 2 * mdx + W2 * px);
                    dm[1].y = (int)(my - L / 2 * mdy + W2 * py);
                    dm[2].x = (int)(mx - L * mdx);
                    dm[2].y = (int)(my - L * mdy);
                    dm[3].x = (int)(mx - L / 2 * mdx - W2 * px);
                    dm[3].y = (int)(my - L / 2 * mdy - W2 * py);
                    dm[4] = dm[0];
                    HBRUSH db = ed->rel == MRE_COMPOSE
                        ? MerBrushC(th->merEdge)
                        : (HBRUSH)GetStockObject(NULL_BRUSH);
                    HBRUSH ob3 = (HBRUSH)SelectObject(hdc, db);
                    if (!MerAAPolygonDC(hdc, dm, 5)) Polygon(hdc, dm, 5);
                    SelectObject(hdc, ob3);
                    break;
                }
                case MRE_ASSOC:
                case MRE_DEPEND:
                    DrawArrowHead(hdc, th->merEdge, FALSE, mx, my, mdx, mdy);
                    break;
                default:
                    if (ed->arrow)
                        DrawArrowHead(hdc, th->merEdge, TRUE, x2, y2, adx, ady);
            }
        }
        if (ed->bidir)
            DrawArrowHead(hdc, th->merEdge, TRUE, x1, y1, -adx, -ady);
        SelectObject(hdc, op);
        if (ed->cardA[0] || ed->cardB[0]) {
            SetTextColor(hdc, th->fgMuted);
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            SetBkMode(hdc, TRANSPARENT);
            if (ed->cardA[0])
                TextOutW(hdc, x1 + UI_Scale(4), y1 - FontH(hdc, f->sm) - UI_Scale(2),
                         ed->cardA, (int)wcslen(ed->cardA));
            if (ed->cardB[0])
                TextOutW(hdc, x2 + UI_Scale(4), y2 - FontH(hdc, f->sm) - UI_Scale(2),
                         ed->cardB, (int)wcslen(ed->cardB));
            SelectObject(hdc, of);
        }
        int dupLbl = 0;
        if (ed->label[0] && !useDagre) {
            for (int q = 0; q < e; q++) {
                MerEdge* qe = &d->edges[q];
                if (qe->a == ed->a && qe->label[0] && wcscmp(qe->label, ed->label) == 0) {
                    dupLbl = 1;
                    break;
                }
            }
        }
        if (ed->label[0] && !dupLbl) {
            int mx = (lbx >= 0) ? lbx : (x1 + x2) / 2;
            int my = (lby >= 0) ? lby : (y1 + y2) / 2;
            SIZE sz;
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            GetTextExtentPoint32W(hdc, ed->label, (int)wcslen(ed->label), &sz);
            int px2 = (d->kind == 2) ? 5 : 3, py2 = (d->kind == 2) ? 3 : 1;
            RECT chip = { mx - sz.cx / 2 - px2, my - sz.cy / 2 - py2,
                          mx + sz.cx / 2 + px2, my + sz.cy / 2 + py2 };
            RECT clashTmp;
            RECT base = chip;
            for (int tries = 0; tries < 25; tries++) {
                if (tries) {
                    static const int dx8[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
                    static const int dy8[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
                    int ring = (tries - 1) / 8 + 1;
                    int dir = (tries - 1) % 8;
                    chip = base;
                    OffsetRect(&chip, dx8[dir] * ring * (sz.cx + UI_Scale(12)),
                               dy8[dir] * ring * ((base.bottom - base.top) + 4));
                }
                if (chip.left < 0 || chip.top < 0) continue;
                BOOL clash = FALSE;
                for (int q = 0; q < nChipUsed && !clash; q++)
                    if (IntersectRect(&clashTmp, &chipUsed[q], &chip)) clash = TRUE;
                for (int q2 = 0; q2 < d->nNodes && !clash; q2++) {
                    RECT nr = { d->nodes[q2].x - UI_Scale(2), d->nodes[q2].y - UI_Scale(2),
                                d->nodes[q2].x + d->nodes[q2].w + UI_Scale(2),
                                d->nodes[q2].y + d->nodes[q2].h + UI_Scale(2) };
                    if (IntersectRect(&clashTmp, &nr, &chip)) clash = TRUE;
                }
                for (int s3 = 0; s3 < d->nSubs && !clash; s3++) {
                    MerSub* sb3 = &d->subs[s3];
                    if (!sb3->w || !sb3->title[0]) continue;
                    RECT tr = { sb3->x, sb3->y,
                                sb3->x + TextW(hdc, f->sm, sb3->title) + UI_Scale(24),
                                sb3->y + UI_Scale(20) };
                    if (IntersectRect(&clashTmp, &tr, &chip)) clash = TRUE;
                }
                if (!clash) break;
            }
            if (nChipUsed < 80) chipUsed[nChipUsed++] = chip;
            SelectObject(hdc, of);
            if (lblE && lblR && nLbl < nEdg) {
                lblR[nLbl] = chip;
                lblE[nLbl] = e;
                nLbl++;
            }
        }
    }
    if (bTgt) {
        for (int i = 0; i < d->nNodes; i++) {
            if (!bTgt[i] || !bCnt || bCnt[i] < 4) continue;
            MerNode* bb = &d->nodes[i];
            int cx = bb->x + bb->w / 2;
            HPEN op2 = (HPEN)SelectObject(hdc, penSolid);
            MerLineAA(hdc, cx, bb->y - bStem, cx, bb->y);
            SelectObject(hdc, op2);
            DrawArrowHead(hdc, th->merEdge, TRUE, cx, bb->y, 0, 1);
        }
    }
    free(bTgt); free(bCnt); free(bSlot); free(bOn);
    free(rTop); free(rBot);
    for (int li = 0; li < nLbl; li++) {
        MerEdge* le = &d->edges[lblE[li]];
        RECT* lr = &lblR[li];
        HFONT lf = (HFONT)SelectObject(hdc, f->sm);
        HBRUSH lb = MerBrushC((d->kind == 2) ? th->selBg : th->merLabelBg);
        FillRect(hdc, lr, lb);
        SetTextColor(hdc, th->merLabelFg);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOutW(hdc, (lr->left + lr->right) / 2, lr->top + 1,
                 le->label, (int)wcslen(le->label));
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        SelectObject(hdc, lf);
    }
    free(lblE); free(lblR);
    SetViewportOrgEx(hdc, voOld.x, voOld.y, NULL);
    DeleteObject(penSolid); DeleteObject(penDash); DeleteObject(penThick);
    HPEN nodePen = CreatePen(PS_SOLID, 1, th->merNodeBorder);
    HBRUSH nodeBr = CreateSolidBrush(th->merNodeFill);
    int stateColorIdx = 0;
    for (int i = 0; i < d->nNodes; i++) {
        MerNode* nd = &d->nodes[i];
        int l = ox + nd->x, t = oy + nd->y, r = l + nd->w, b = t + nd->h;
        HPEN op = (HPEN)SelectObject(hdc, nodePen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, nodeBr);
        HPEN penOwn = NULL; HBRUSH brOwn = NULL;
        int rr = UI_Scale(12);
        if (d->kind == 2 && nd->shape == MER_ROUND) {
            COLORREF pc = StatePal(th, stateColorIdx++);
            penOwn = MerPenC(PS_SOLID, 2, pc);
            brOwn = MerBrushC(MixClr(th->selBg, pc, 0.15));
            SelectObject(hdc, penOwn);
            SelectObject(hdc, brOwn);
            rr = UI_Scale(5);
        }
        switch (nd->shape) {
            case MER_ROUND:
                if (!MerAARoundRectDC(hdc, l, t, r, b, rr, rr)) RoundRect(hdc, l, t, r, b, rr, rr);
                break;
            case MER_STADIUM:
                if (!MerAARoundRectDC(hdc, l, t, r, b, nd->h / 2, nd->h / 2)) RoundRect(hdc, l, t, r, b, nd->h / 2, nd->h / 2);
                break;
            case MER_CYL: {
                int cap = UI_Scale(9);
                if (!MerAARectDC(hdc, l, t + cap, r, b - cap)) Rectangle(hdc, l, t + cap, r, b - cap);
                if (!MerAAEllipseDC(hdc, l, t, r, t + cap * 2)) Ellipse(hdc, l, t, r, t + cap * 2);
                if (!MerAAPieDC(hdc, l, b - cap * 2, r, b, r, b - cap, l, b - cap)) Pie(hdc, l, b - cap * 2, r, b, r, b - cap, l, b - cap);
                break;
            }
            case MER_CIRCLE:
                if (!MerAAEllipseDC(hdc, l, t, r, b)) Ellipse(hdc, l, t, r, b);
                break;
            case MER_START: {
                HPEN sp = MerPenC(PS_SOLID, 2, th->link);
                HPEN op2 = (HPEN)SelectObject(hdc, sp);
                HBRUSH ob2 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                if (!MerAAEllipseDC(hdc, l, t, r, b)) Ellipse(hdc, l, t, r, b);
                SelectObject(hdc, ob2);
                SelectObject(hdc, op2);
                break;
            }
            case MER_END: {
                COLORREF rc = MerPal(th, 5);
                HPEN sp = MerPenC(PS_SOLID, 2, rc);
                HBRUSH sb = MerBrushC(rc);
                HPEN op2 = (HPEN)SelectObject(hdc, sp);
                HBRUSH ob2 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                if (!MerAAEllipseDC(hdc, l, t, r, b)) Ellipse(hdc, l, t, r, b);
                SelectObject(hdc, sb);
                int m = (r - l) * 3 / 10;
                if (!MerAAEllipseDC(hdc, l + m, t + m, r - m, b - m)) Ellipse(hdc, l + m, t + m, r - m, b - m);
                SelectObject(hdc, ob2);
                SelectObject(hdc, op2);
                break;
            }
            case MER_DIAMOND: {
                POINT pts[4] = { { (l + r) / 2, t }, { r, (t + b) / 2 },
                                 { (l + r) / 2, b }, { l, (t + b) / 2 } };
                if (!MerAAPolygonDC(hdc, pts, 4)) Polygon(hdc, pts, 4);
                break;
            }
            case MER_FLAG: {
                POINT pts[5] = { { l, t }, { r - UI_Scale(10), t }, { r, (t + b) / 2 },
                                 { r - UI_Scale(10), b }, { l, b } };
                if (!MerAAPolygonDC(hdc, pts, 5)) Polygon(hdc, pts, 5);
                break;
            }
            default:
                if (!MerAARectDC(hdc, l, t, r, b)) Rectangle(hdc, l, t, r, b);
        }
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        SetTextColor(hdc, th->merNodeText);
        if ((d->kind == 3 || d->kind == 4) && nd->extra) {
            int lineH = FontH(hdc, f->sm) + UI_Scale(2);
            int sep = UI_Scale(1);
            RECT rc0 = { l, t, r, t + lineH + UI_Scale(8) };
            HFONT of = (HFONT)SelectObject(hdc, f->bold);
            DrawTextW(hdc, nd->label, -1, &rc0, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, of);
            int yy = rc0.bottom;
            HPEN op2 = (HPEN)SelectObject(hdc, nodePen);
            MerLineAA(hdc, l, yy, r, yy);
            wchar_t (*mem)[96] = (wchar_t(*)[96])nd->extra;
            HFONT of2 = (HFONT)SelectObject(hdc, f->sm);
            int total = nd->nExtraA + nd->nExtraB;
            for (int m2 = 0; m2 < total; m2++) {
                if (m2 == nd->nExtraA && d->kind == 3) {
                    yy += sep;
                    MerLineAA(hdc, l, yy, r, yy);
                }
                RECT rm = { l + UI_Scale(8), yy, r - UI_Scale(4), yy + lineH };
                SetTextColor(hdc, th->merNodeText);
                TextOutW(hdc, rm.left, yy + UI_Scale(1), mem[m2], (int)wcslen(mem[m2]));
                yy += lineH;
            }
            SelectObject(hdc, of2);
            SelectObject(hdc, op2);
        } else if (nd->shape != MER_START && nd->shape != MER_END) {
            int topPad = (nd->shape == MER_CYL) ? UI_Scale(13) : UI_Scale(2);
            RECT rc = { l + UI_Scale(4), t + topPad, r - UI_Scale(4), b - UI_Scale(2) };
            HFONT of = (HFONT)SelectObject(hdc, f->body);
            UINT fmt = DT_CENTER | DT_VCENTER | DT_NOPREFIX;
            if (wcschr(nd->label, L'\n'))
                fmt |= DT_WORDBREAK | DT_EDITCONTROL;
            else
                fmt |= DT_SINGLELINE | DT_END_ELLIPSIS;
            DrawTextW(hdc, nd->label, -1, &rc, fmt);
            SelectObject(hdc, of);
        }
    }
    DeleteObject(nodePen);
    DeleteObject(nodeBr);
    free(inTopCnt);
    free(outTopCnt);
    free(inSlot);
    free(outSlot);
    free(eList);
}
static const wchar_t* FrameKindLabel(MerFrameKind k) {
    switch (k) {
        case MSF_LOOP: return L"loop";
        case MSF_ALT: return L"alt";
        case MSF_OPT: return L"opt";
        case MSF_PAR: return L"par";
        case MSF_CRITICAL: return L"critical";
        case MSF_BREAK: return L"break";
        case MSF_RECT: return L"";
        default: return L"";
    }
}
static void SeqDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    SetBkMode(hdc, TRANSPARENT);
    int margin = UI_Scale(12);
    int boxH = FontH(hdc, f->bold) + UI_Scale(12);
    int indent = 0;
    for (int i = 0; i < d->nItems; i++) {
        MerSeqItem* it = &d->items[i];
        if (it->type == MSI_FRAME_BEGIN) {
            int depth = 1, j = i + 1;
            int yEnd = it->y;
            for (; j < d->nItems; j++) {
                MerSeqItem* jt = &d->items[j];
                if (jt->type == MSI_FRAME_BEGIN) depth++;
                else if (jt->type == MSI_FRAME_END) { if (--depth == 0) { yEnd = jt->y; break; } }
                else if (jt->type == MSI_FRAME_ELSE && depth == 1) yEnd = jt->y;
            }
            if (j >= d->nItems) yEnd = it->y + UI_Scale(40);
            int l = ox + margin + indent, r = ox + d->sz.cx - margin - indent;
            int t = oy + it->y - UI_Scale(4), b = oy + yEnd;
            HPEN pen = MerPenC(PS_SOLID, 1, th->frame);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
            if (!MerAARoundRectDC(hdc, l, t, r, b, UI_Scale(6), UI_Scale(6))) RoundRect(hdc, l, t, r, b, UI_Scale(6), UI_Scale(6));
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            if (it->kind != MSF_RECT) {
                const wchar_t* kindLabel = FrameKindLabel(it->kind);
                wchar_t buf[200];
                if (it->label[0])
                    _snwprintf(buf, 199, L"%s [%s]", kindLabel, it->label);
                else
                    _snwprintf(buf, 199, L"%s", kindLabel);
                SetTextColor(hdc, th->fgMuted);
                HFONT of = (HFONT)SelectObject(hdc, f->sm);
                TextOutW(hdc, l + UI_Scale(8), t + UI_Scale(3), buf, (int)wcslen(buf));
                SelectObject(hdc, of);
            }
            indent += UI_Scale(10);
        } else if (it->type == MSI_FRAME_END) {
            if (indent >= UI_Scale(10)) indent -= UI_Scale(10);
        } else if (it->type == MSI_FRAME_ELSE) {
            int l = ox + margin + indent, r = ox + d->sz.cx - margin - indent;
            HPEN pen = MerPenC(PS_DOT, 1, th->frame);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            MerLineAA(hdc, l, oy + it->y, r, oy + it->y);
            SelectObject(hdc, op);
            wchar_t buf[180];
            _snwprintf(buf, 179, L"else%s%s", it->label[0] ? L" [" : L"", it->label);
            SetTextColor(hdc, th->fgMuted);
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            TextOutW(hdc, l + UI_Scale(8), oy + it->y + UI_Scale(2), buf, (int)wcslen(buf));
            SelectObject(hdc, of);
        }
    }
    HPEN lifePen = CreatePen(PS_DOT, 1, th->fgMuted);
    HPEN op0 = (HPEN)SelectObject(hdc, lifePen);
    for (int i = 0; i < d->nParts; i++) {
        int x = ox + d->parts[i].x;
        MerLineAA(hdc, x, oy + margin + boxH, x, oy + d->sz.cy - margin);
    }
    SelectObject(hdc, op0);
    DeleteObject(lifePen);
    HPEN boxPen = CreatePen(PS_SOLID, 1, th->merNodeBorder);
    HBRUSH boxBr = CreateSolidBrush(th->merNodeFill);
    for (int i = 0; i < d->nParts; i++) {
        int x = ox + d->parts[i].x;
        int w = TextW(hdc, f->bold, d->parts[i].name) + UI_Scale(24);
        if (w < UI_Scale(90)) w = UI_Scale(90);
        RECT rc = { x - w / 2, oy + margin, x + w / 2, oy + margin + boxH };
        HPEN op = (HPEN)SelectObject(hdc, boxPen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, boxBr);
        if (!MerAARectDC(hdc, rc.left, rc.top, rc.right, rc.bottom)) Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        SetTextColor(hdc, th->merNodeText);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        DrawTextW(hdc, d->parts[i].name, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, of);
    }
    DeleteObject(boxPen);
    DeleteObject(boxBr);
    HPEN msgPen = CreatePen(PS_SOLID, 1, th->merEdge);
    HPEN msgDash = CreatePen(PS_DASH, 1, th->merEdge);
    HBRUSH noteBr = CreateSolidBrush(th->seqNoteBg);
    HPEN notePen = CreatePen(PS_SOLID, 1, th->seqNoteBorder);
    for (int i = 0; i < d->nItems; i++) {
        MerSeqItem* it = &d->items[i];
        if (it->type == MSI_MSG) {
            int y = oy + it->y + it->h / 2;
            int x1 = ox + d->parts[it->from].x;
            int x2 = ox + d->parts[it->to].x;
            HPEN pen = it->dash ? msgDash : msgPen;
            HPEN op = (HPEN)SelectObject(hdc, pen);
            if (it->from == it->to) {
                int hook = UI_Scale(36);
                POINT pts[4] = { { x1, y - UI_Scale(4) }, { x1 + hook, y - UI_Scale(4) },
                                 { x1 + hook, y + UI_Scale(6) },
                                 { x1 + UI_Scale(2), y + UI_Scale(6) } };
                if (!MerAAPolylineDC(hdc, pts, 4)) {
                    Polyline(hdc, pts, 3);
                    LineTo(hdc, x1 + UI_Scale(2), y + UI_Scale(6));
                }
                DrawArrowHead(hdc, th->merEdge, it->head == 0, x1, y + UI_Scale(6), -1, 0);
                SelectObject(hdc, op);
                if (it->text[0]) {
                    SetTextColor(hdc, th->merNodeText);
                    HFONT of = (HFONT)SelectObject(hdc, f->sm);
                    SetTextAlign(hdc, TA_LEFT | TA_BOTTOM);
                    TextOutW(hdc, x1 + UI_Scale(6), y - UI_Scale(6), it->text, (int)wcslen(it->text));
                    SetTextAlign(hdc, TA_LEFT | TA_TOP);
                    SelectObject(hdc, of);
                }
                continue;
            }
            MerLineAA(hdc, x1, y, x2, y);
            double fx = x2 - x1, fy = 0;
            if (it->head == 2) {
                int s = UI_Scale(5);
                MerLineAA(hdc, x2 - s, y - s, x2 + s, y + s);
                MerLineAA(hdc, x2 - s, y + s, x2 + s, y - s);
            } else {
                DrawArrowHead(hdc, th->merEdge, it->head == 0, x2, y, fx, fy);
            }
            SelectObject(hdc, op);
            if (it->text[0]) {
                SetTextColor(hdc, th->merNodeText);
                HFONT of = (HFONT)SelectObject(hdc, f->sm);
                SetTextAlign(hdc, TA_CENTER | TA_BOTTOM);
                TextOutW(hdc, (x1 + x2) / 2, y - 2, it->text, (int)wcslen(it->text));
                SetTextAlign(hdc, TA_LEFT | TA_TOP);
                SelectObject(hdc, of);
            }
        } else if (it->type == MSI_NOTE) {
            int noteW = UI_Scale(170);
            int px = ox + d->parts[it->from].x;
            int l, r;
            if (it->noteSide == 0) { r = px - UI_Scale(8); l = r - noteW; }
            else if (it->noteSide == 1) { l = px + UI_Scale(8); r = l + noteW; }
            else {
                int p2 = it->over2 >= 0 ? ox + d->parts[it->over2].x : px;
                int mn = px < p2 ? px : p2, mx = px > p2 ? px : p2;
                l = (mn + mx) / 2 - noteW / 2;
                r = l + noteW;
            }
            RECT rc = { l, oy + it->y, r, oy + it->y + it->h };
            HPEN op = (HPEN)SelectObject(hdc, notePen);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, noteBr);
            if (!MerAARectDC(hdc, rc.left, rc.top, rc.right, rc.bottom)) Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            InflateRect(&rc, -UI_Scale(6), -UI_Scale(5));
            SetTextColor(hdc, th->seqNoteFg);
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            DrawTextW(hdc, it->text, -1, &rc, DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
            SelectObject(hdc, of);
        }
    }
    DeleteObject(msgPen); DeleteObject(msgDash);
    DeleteObject(noteBr); DeleteObject(notePen);
}
SIZE Mermaid_Measure(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    if (!d) { SIZE z = { 0, 0 }; return z; }
    if (d->kind == 0 || (d->kind >= 2 && d->kind <= 4)) FlowLayout(d, hdc, f);
    else if (d->kind == 1) SeqLayout(d, hdc, f);
    else ExtLayout(d, hdc, f);
    d->sz.cx += UI_Scale(4);
    d->sz.cy += UI_Scale(4);
    return d->sz;
}
/* 缓存位图四周留一圈，避免图的边缘元素（箭头、标签底、圆角）被裁掉。 */
static int MerCachePad(void) {
    return UI_Scale(12);
}

/* 设 MER_NOCACHE=1 可以关掉缓存，用于和直接绘制逐像素对拍。 */
static BOOL MerCacheOn(void) {
    static int st = -1;
    if (st < 0) st = getenv("MER_NOCACHE") ? 0 : 1;
    return st ? TRUE : FALSE;
}

static void MerCacheDrop(MermaidDiagram* d) {
    if (!d) return;
    if (d->cDc) {
        if (d->cBmp) {
            SelectObject(d->cDc, d->cOldBmp);
            DeleteObject(d->cBmp);
        }
        DeleteDC(d->cDc);
    }
    d->cDc = NULL;
    d->cBmp = NULL;
    d->cOldBmp = NULL;
    d->cW = d->cH = 0;
    d->cKey = 0;
}

static DWORD MerMixKey(DWORD h, DWORD v) {
    h ^= v;
    h *= 16777619u;
    return h;
}

/* 主题、字体、DPI 任何一样变了都要重画。字体句柄在字体变更时会重新创建，
   所以拿句柄当指纹就够了，不用额外通知。 */
static DWORD MerCacheKey(const MdFonts* f, const MdTheme* th) {
    DWORD h = 2166136261u;
    h = MerMixKey(h, th->bg);
    h = MerMixKey(h, th->fg);
    h = MerMixKey(h, th->fgMuted);
    h = MerMixKey(h, th->link);
    h = MerMixKey(h, th->frame);
    h = MerMixKey(h, th->tableHeadBg);
    h = MerMixKey(h, th->selBg);
    h = MerMixKey(h, th->merNodeFill);
    h = MerMixKey(h, th->merNodeBorder);
    h = MerMixKey(h, th->merNodeText);
    h = MerMixKey(h, th->merEdge);
    h = MerMixKey(h, th->merLabelBg);
    h = MerMixKey(h, th->merLabelFg);
    h = MerMixKey(h, th->seqNoteBg);
    h = MerMixKey(h, th->seqNoteBorder);
    h = MerMixKey(h, th->seqNoteFg);
    h = MerMixKey(h, (DWORD)(ULONG_PTR)f->body);
    h = MerMixKey(h, (DWORD)(ULONG_PTR)f->sm);
    h = MerMixKey(h, (DWORD)(ULONG_PTR)f->bold);
    h = MerMixKey(h, (DWORD)UI_Scale(100));
    return h;
}

static void MerDrawContent(MermaidDiagram* d, HDC hdc, int x, int y,
                           const MdFonts* f, const MdTheme* th) {
    if (d->kind == 0 || (d->kind >= 2 && d->kind <= 4)) FlowDraw(d, hdc, x, y, f, th);
    else if (d->kind == 1) SeqDraw(d, hdc, x, y, f, th);
    else ExtDraw(d, hdc, x, y, f, th);
}

void Mermaid_Draw(MermaidDiagram* d, HDC hdc, int x, int y,
                  const MdFonts* f, const MdTheme* th) {
    if (!d) return;
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    /* 图完全不在裁剪区里就别画了，长文档里大多数图都属于这种情况 */
    if (d->sz.cx > 0 && d->sz.cy > 0) {
        RECT box, self, hit;
        self.left = x;
        self.top = y;
        self.right = x + d->sz.cx;
        self.bottom = y + d->sz.cy;
        if (GetClipBox(hdc, &box) != ERROR && !IntersectRect(&hit, &box, &self)) return;
    }
    if (MerCacheOn() && d->sz.cx > 0 && d->sz.cy > 0) {
        int pad = MerCachePad();
        DWORD key = MerCacheKey(f, th);
        if (!d->cBmp || d->cW != d->sz.cx || d->cH != d->sz.cy || d->cKey != key) {
            MerCacheDrop(d);
            {
                HDC dc = CreateCompatibleDC(hdc);
                HBITMAP bmp = dc ? CreateCompatibleBitmap(hdc, d->sz.cx + pad * 2,
                                                          d->sz.cy + pad * 2) : NULL;
                if (dc && bmp) {
                    HBRUSH bg = CreateSolidBrush(th->bg);
                    RECT r = { 0, 0, d->sz.cx + pad * 2, d->sz.cy + pad * 2 };
                    HBITMAP old = (HBITMAP)SelectObject(dc, bmp);
                    FillRect(dc, &r, bg);
                    DeleteObject(bg);
                    MerDrawContent(d, dc, pad, pad, f, th);
                    GdiFlush();
                    d->cDc = dc;
                    d->cBmp = bmp;
                    d->cOldBmp = old;
                    d->cW = d->sz.cx;
                    d->cH = d->sz.cy;
                    d->cKey = key;
                } else {
                    if (bmp) DeleteObject(bmp);
                    if (dc) DeleteDC(dc);
                }
            }
        }
        if (d->cBmp) {
            /* 只贴图的有效范围：位图里多出的 pad 只是为了防止边缘元素被裁，
               不能一起贴到画布上，否则会盖掉图周围的正文。 */
            BitBlt(hdc, x, y, d->cW, d->cH, d->cDc, pad, pad, SRCCOPY);
            return;
        }
    }
    MerDrawContent(d, hdc, x, y, f, th);
}
#include "mermaid_ext1.inc"
#include "mermaid_ext2.inc"
#include "mermaid_ext3.inc"

