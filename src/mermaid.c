/* mermaid.c — Mermaid 图表原生渲染（GDI 自绘，零 Web 引擎）
   参照 tinta 的纯原生路线。本期支持两个最常用图族：
     - flowchart / graph（TB/BT/LR/RL、常见节点形状、带标签边、subgraph）
     - sequenceDiagram（participant/actor、实/虚线消息、Note、loop/alt/opt 等框）
   其余图族 Mermaid_Parse 返回 NULL，由 mdview 回退为代码块显示。
   解析容错：无法识别的行一律忽略，不中断渲染。 */

#include "common.h"
#include <math.h>

/* ============================ 通用小工具 ============================ */

static wchar_t* TrimW(wchar_t* s) {
    while (*s == L' ' || *s == L'\t') s++;
    size_t n = wcslen(s);
    while (n > 0 && (s[n-1] == L' ' || s[n-1] == L'\t' || s[n-1] == L'\r')) s[--n] = L'\0';
    return s;
}

/* 取一行 UTF-8 → 宽字符（调用方给出目的缓冲；返回 FALSE 表示空行） */
static BOOL LineToWide(const char* src, int len, int* ppos, wchar_t* out, int cch) {
    int pos = *ppos, s = pos;
    while (pos < len && src[pos] != '\n') pos++;
    int lineLen = pos - s;
    if (pos < len) pos++;                       /* 跳过 \n */
    *ppos = pos;
    /* 去掉 \r */
    while (lineLen > 0 && (src[s + lineLen - 1] == '\r')) lineLen--;
    int n = MultiByteToWideChar(CP_UTF8, 0, src + s, lineLen, out, cch - 1);
    out[n ? n : 0] = L'\0';
    return n > 0;
}

static int TextW(HDC hdc, HFONT f, const wchar_t* s) {
    HFONT of = (HFONT)SelectObject(hdc, f);
    SIZE sz; 
    GetTextExtentPoint32W(hdc, s, (int)wcslen(s), &sz);
    SelectObject(hdc, of);
    return sz.cx;
}

static int FontH(HDC hdc, HFONT f) {
    HFONT of = (HFONT)SelectObject(hdc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, of);
    return tm.tmHeight;
}

/* 节点形状 */
typedef enum { MER_RECT, MER_ROUND, MER_CIRCLE, MER_DIAMOND, MER_FLAG,
               MER_START, MER_END, MER_STADIUM } MerShape;

/* 关系线样式（class/er） */
typedef enum { MRE_PLAIN, MRE_INHERIT, MRE_COMPOSE, MRE_AGGREG,
               MRE_ASSOC, MRE_DEPEND } MerRel;

/* ============================ 数据结构 ============================ */

typedef struct MerNode {
    wchar_t id[64];
    wchar_t label[256];
    MerShape shape;
    int sub;                 /* 所属 subgraph 下标，-1 = 无 */
    int rank;
    int x, y, w, h;          /* 布局结果（左上角 + 尺寸） */
    int wFix, hFix;          /* 预设尺寸（class/er 成员框，>0 优先） */
    void* extra;             /* class/er：成员行数组（自有，wchar_t(*)[96]） */
    int nExtraA, nExtraB;    /* 成员计数（属性 / 方法） */
} MerNode;

typedef struct MerEdge {
    int a, b;                /* 节点下标 */
    BOOL arrow, dash, thick;
    wchar_t label[160];
    wchar_t cardA[16], cardB[16];   /* 两端多重性文本 */
    MerRel rel;
    BOOL markA;              /* 类图：标记画在 a 端（<|-- 等左标记算子） */
} MerEdge;

typedef struct MerSub {
    wchar_t title[160];
    int x, y, w, h;
} MerSub;

/* 时序图条目 */
typedef enum { MSI_MSG, MSI_NOTE, MSI_FRAME_BEGIN, MSI_FRAME_ELSE, MSI_FRAME_END } MerSeqType;
typedef enum { MSF_LOOP, MSF_ALT, MSF_OPT, MSF_PAR, MSF_CRITICAL, MSF_BREAK, MSF_RECT, MSF_ELSE } MerFrameKind;

typedef struct MerPart { wchar_t id[64], name[256]; int x; } MerPart;

typedef struct MerSeqItem {
    MerSeqType type;
    int from, to;            /* 消息两端（participant 下标） */
    BOOL dash;               /* 虚线 */
    int head;                /* 0=实心 1=空心 2=叉 */
    wchar_t text[256];       /* 消息文本 / 备注文本 */
    int noteSide;            /* 0=left of 1=right of 2=over */
    int over2;               /* over A,B 的第二个 participant，-1 无 */
    MerFrameKind kind;
    wchar_t label[160];
    int y, h;                /* 布局结果 */
} MerSeqItem;

struct MermaidDiagram {
    int kind;                /* 0=flowchart 1=sequence 2=state 3=class 4=er
                                5=pie 6=quadrant 7=timeline 8=journey
                                9=gantt 10=xychart 11=mindmap 12=git */
    /* flowchart（state/class/er 复用） */
    MerNode* nodes; int nNodes;
    MerEdge* edges; int nEdges;
    MerSub* subs; int nSubs;
    int dirLR, dirBT;
    /* sequence */
    MerPart* parts; int nParts;
    MerSeqItem* items; int nItems;
    void* ext;               /* 各图族私有结构（自有） */
    /* 布局缓存 */
    SIZE sz;
};

/* ============================ 动态数组 ============================ */

static void* MerGrow(void* p, int* cap, int need, int elem) {
    if (need < *cap) return p;
    int nc = *cap ? *cap * 2 : 16;
    while (nc < need) nc *= 2;
    void* q = realloc(p, (size_t)nc * elem);
    *cap = nc;
    return q;
}

/* ============================ 流程图解析 ============================ */

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
        if (label[0]) { wcscpy_s(c->ns[ex].label, 256, label); c->ns[ex].shape = shape; }
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
    e->a = a; e->b = b;
    e->arrow = arrow; e->dash = dash; e->thick = thick;
    wcscpy_s(e->label, 160, label ? label : L"");
}

/* 从 line[*pos] 解析一个节点记号：id + 可选形状括号。
   成功返回节点句柄所需信息并推进 *pos；失败返回 FALSE（*pos 不动）。 */
static BOOL FlowParseNode(const wchar_t* line, int* pos, wchar_t* id, wchar_t* label, int* shape) {
    int p = *pos;
    while (line[p] == L' ' || line[p] == L'\t') p++;
    if (!line[p]) return FALSE;
    /* id：读到空白/形状括号/边算子起点 */
    int i = 0;
    while (line[p] && i < 63) {
        wchar_t ch = line[p];
        if (ch == L' ' || ch == L'\t' || ch == L'[' || ch == L'{' || ch == L'(' ||
            ch == L'>' || ch == L'|' || ch == L',' || ch == L'&') break;
        if (ch == L'-' && (line[p+1] == L'-' || line[p+1] == L'.')) break;
        if (ch == L'=' && line[p+1] == L'=') break;
        id[i++] = ch; p++;
    }
    if (i == 0) return FALSE;
    id[i] = L'\0';
    *shape = MER_RECT;
    label[0] = L'\0';
    /* 形状括号（允许与 id 之间有空格） */
    int q = p;
    while (line[q] == L' ' || line[q] == L'\t') q++;
    static const struct { const wchar_t* open; const wchar_t* close; int shape; } pairs[] = {
        { L"((", L"))", MER_CIRCLE },
        { L"[(", L")]", MER_RECT },      /* 圆柱 → 矩形近似 */
        { L"[/", L"/]", MER_RECT },      /* 平行四边形 → 矩形近似 */
        { L"[\\", L"\\]", MER_RECT },
        { L"[",  L"]",  MER_RECT },
        { L"(",  L")",  MER_ROUND },
        { L"{",  L"}",  MER_DIAMOND },
        { L">",  L"]",  MER_FLAG },
    };
    for (int k = 0; k < (int)(sizeof(pairs)/sizeof(pairs[0])); k++) {
        int ol = (int)wcslen(pairs[k].open);
        if (wcsncmp(line + q, pairs[k].open, ol) != 0) continue;
        const wchar_t* end = wcsstr(line + q + ol, pairs[k].close);
        if (!end) break;
        int len = (int)(end - (line + q + ol));
        if (len >= 256) len = 255;
        if (len > 0 && line[q+ol] == L'"' && end[-1] == L'"' && len >= 2) {
            len -= 2; memcpy(label, line + q + ol + 1, (size_t)len * sizeof(wchar_t)); label[len] = 0;
        } else {
            memcpy(label, line + q + ol, (size_t)len * sizeof(wchar_t)); label[len] = 0;
        }
        *shape = pairs[k].shape;
        p = (int)(end - line) + (int)wcslen(pairs[k].close);
        *pos = p;
        return TRUE;
    }
    *pos = p;
    return TRUE;
}

/* 解析边算子；支持 --> --- -.- -.-> ==> 以及 |标签| 和内联文本（A -- t --> B） */
static BOOL FlowParseOp(const wchar_t* s, int* pos, BOOL* arrow, BOOL* dash, BOOL* thick, wchar_t* label) {
    int p = *pos;
    if (!(s[p] == L'-' || s[p] == L'=')) return FALSE;
    if (s[p] == L'-' && s[p+1] != L'-' && s[p+1] != L'.') return FALSE;
    if (s[p] == L'=' && s[p+1] != L'=') return FALSE;
    int start = p;
    while (s[p] == L'-' || s[p] == L'.' || s[p] == L'=') p++;
    *arrow = FALSE;
    if (s[p] == L'>') { *arrow = TRUE; p++; }
    *dash = FALSE; *thick = FALSE;
    for (int q2 = start; q2 < p - (*arrow ? 1 : 0); q2++) {
        if (s[q2] == L'.') *dash = TRUE;
        if (s[q2] == L'=') *thick = TRUE;
    }
    label[0] = L'\0';
    int after = p;
    while (s[p] == L' ' || s[p] == L'\t') p++;
    if (s[p] == L'|') {                                   /* -->|label| */
        const wchar_t* e = wcschr(s + p + 1, L'|');
        if (e) {
            int len = (int)(e - (s + p + 1));
            if (len > 159) len = 159;
            memcpy(label, s + p + 1, (size_t)len * sizeof(wchar_t));
            label[len] = 0;
            p = (int)(e - s) + 1;
            *pos = p;
            return TRUE;
        }
    }
    if (!*arrow) {
        /* 内联文本形式：向后找下一个以 '>' 结尾的算子，中间文本即标签 */
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

    /* 语句：LHS 节点[,节点] op RHS 节点[& 节点]（可链式） */
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
    while (s[pos]) {
        BOOL arrow, dash, thick; wchar_t elabel[160];
        if (!FlowParseOp(s, &pos, &arrow, &dash, &thick, elabel)) break;
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
            for (int j = 0; j < nRhs; j++)
                FlowAddEdge(c, lhs[i], rhs[j], arrow, dash, thick, elabel);
        /* 链式：A --> B --> C */
        nLhs = 1; lhs[0] = rhs[nRhs - 1];
        while (s[pos] == L' ' || s[pos] == L'\t') pos++;
    }
}

/* ============================ 时序图解析 ============================ */

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

/* 消息左半部分（A op B）解析 */
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
        /* ids: "A" 或 "A,B" */
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
        while (*rest && *rest != L' ' && *rest != L'\t') rest++;   /* 跳过关键字 */
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

    /* 消息：A ->> B: text */
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

/* ============================ 入口：解析 ============================ */

/* 扩展图族（state/class/er/pie/quadrant/timeline/journey/gantt/xychart/mindmap/git，
   实现见文件后部 */
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

    /* 首个非空行判断图族 */
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
        return NULL;   /* 未知图族 */
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
        /* 未闭合的框补 end */
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
    ExtFree(d);
    free(d->nodes); free(d->edges); free(d->subs);
    free(d->parts); free(d->items);
    free(d);
}

/* ============================ 布局：流程图 ============================ */

static void FlowLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    int n = d->nNodes;
    int padX = UI_Scale(14), padY = UI_Scale(7);
    int hgap = UI_Scale(44), vgap = UI_Scale(52), margin = UI_Scale(12);

    if (d->kind == 3 || d->kind == 4)   /* class/er：按成员文本实测尺寸 */
        ClassErFixSizeReal(d, hdc, f);

    /* 节点尺寸 */
    for (int i = 0; i < n; i++) {
        MerNode* nd = &d->nodes[i];
        if (nd->wFix > 0 && nd->hFix > 0) {   /* class/er 预设成员框尺寸 */
            nd->w = nd->wFix;
            nd->h = nd->hFix;
            continue;
        }
        int tw = TextW(hdc, f->body, nd->label);
        int th = FontH(hdc, f->body);
        nd->w = tw + padX * 2;
        nd->h = th + padY * 2;
        if (nd->w < nd->h) nd->w = nd->h;
        if (nd->shape == MER_CIRCLE) {
            int m = nd->w > nd->h ? nd->w : nd->h;
            nd->w = nd->h = m;
        } else if (nd->shape == MER_DIAMOND) {
            nd->w = (tw + padX * 2) * 2;
            nd->h = (th + padY * 2) * 2;
        }
    }

    /* 分层：最长路径松弛（有环时经 n 次迭代截断） */
    for (int pass = 0; pass < n; pass++) {
        BOOL changed = FALSE;
        for (int i = 0; i < d->nEdges; i++) {
            MerNode* a = &d->nodes[d->edges[i].a];
            MerNode* b = &d->nodes[d->edges[i].b];
            if (b->rank < a->rank + 1) { b->rank = a->rank + 1; changed = TRUE; }
        }
        if (!changed) break;
    }

    /* 分组 */
    int maxRank = 0;
    for (int i = 0; i < n; i++)
        if (d->nodes[i].rank > maxRank) maxRank = d->nodes[i].rank;
    int nr = maxRank + 1;
    int* cnt = (int*)calloc((size_t)nr, sizeof(int));
    int* rankOf = (int*)calloc((size_t)n, sizeof(int));   /* 每个节点在层内的序号 */
    int** buckets = (int**)calloc((size_t)nr, sizeof(int*));
    for (int i = 0; i < n; i++) cnt[d->nodes[i].rank]++;
    for (int r = 0; r < nr; r++) buckets[r] = (int*)calloc((size_t)cnt[r] ? (size_t)cnt[r] : 1, sizeof(int));
    int* fill = (int*)calloc((size_t)nr, sizeof(int));
    for (int i = 0; i < n; i++) {
        int r = d->nodes[i].rank;
        rankOf[i] = fill[r];
        buckets[r][fill[r]++] = i;
    }

    /* 层内排序：邻层重心，两轮扫描 */
    for (int sweep = 0; sweep < 4; sweep++) {
        for (int r = 1; r < nr; r++) {
            /* 用上一层顺序号做重心 */
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
            }
            /* 按重心稳定排序（插入排序，保持相对次序） */
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

    /* 位置：TB —— rank 决定主轴，层内顺序决定副轴 */
    int* rankSize = (int*)calloc((size_t)nr, sizeof(int));   /* 副轴总宽 */
    int* rankMain = (int*)calloc((size_t)nr, sizeof(int));   /* 主轴尺寸（层内节点主轴尺寸最大值） */
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
            int mainOff = (rankMain[r] - mainSize) / 2;    /* 层内主轴居中 */
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

    /* BT/RL：镜像主轴 / 副轴 */
    if (d->dirBT) {
        for (int i = 0; i < n; i++) d->nodes[i].y = mainTotal + margin * 2 - d->nodes[i].y - d->nodes[i].h;
    }
    if (d->dirLR && d->dirBT) {
        int maxX2 = 0;
        for (int i = 0; i < n; i++)
            if (d->nodes[i].x + d->nodes[i].w > maxX2) maxX2 = d->nodes[i].x + d->nodes[i].w;
        for (int i = 0; i < n; i++) d->nodes[i].x = maxX2 - d->nodes[i].x - d->nodes[i].w;
    }

    /* subgraph 边界 */
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

    /* 尺寸 */
    int maxX = 0, maxY = 0;
    for (int i = 0; i < n; i++) {
        if (d->nodes[i].x + d->nodes[i].w > maxX) maxX = d->nodes[i].x + d->nodes[i].w;
        if (d->nodes[i].y + d->nodes[i].h > maxY) maxY = d->nodes[i].y + d->nodes[i].h;
    }
    for (int s = 0; s < d->nSubs; s++) {
        if (!d->subs[s].w) continue;
        if (d->subs[s].x + d->subs[s].w > maxX) maxX = d->subs[s].x + d->subs[s].w;
        if (d->subs[s].y + d->subs[s].h > maxY) maxY = d->subs[s].y + d->subs[s].h;
        if (d->subs[s].x < 0) { /* 不让子图越界 */ }
    }
    /* 边标签略微外扩 */
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

/* ============================ 布局：时序图 ============================ */

static void SeqLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    int margin = UI_Scale(12);
    int boxH = FontH(hdc, f->bold) + UI_Scale(12);
    int gap = UI_Scale(64);
    int itemH = UI_Scale(26);
    int noteW = UI_Scale(170);

    /* 参与者列宽 */
    int totalW = margin * 2;
    for (int i = 0; i < d->nParts; i++) {
        int w = TextW(hdc, f->bold, d->parts[i].name) + UI_Scale(24);
        if (w < UI_Scale(90)) w = UI_Scale(90);
        d->parts[i].x = w;    /* 暂存列宽 */
    }
    for (int i = 0; i < d->nParts; i++)
        totalW += d->parts[i].x + (i + 1 < d->nParts ? gap : 0);

    int x = margin;
    for (int i = 0; i < d->nParts; i++) {
        int w = d->parts[i].x;
        d->parts[i].x = x + w / 2;   /* 生命线中心 */
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
                /* 用 DrawText 精确测高（与绘制参数一致） */
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
    if (depth > 0) y += UI_Scale(24);   /* 未闭合框兜底 */

    d->sz.cx = totalW;
    d->sz.cy = y + margin;
}

/* ============================ 绘制工具 ============================ */

static void DrawArrowHead(HDC hdc, COLORREF clr, BOOL filled, int x, int y, double dx, double dy) {
    double len = sqrt(dx * dx + dy * dy);
    if (len < 0.001) return;
    dx /= len; dy /= len;
    double px = -dy, py = dx;
    int size = UI_Scale(10), hw = UI_Scale(4);
    POINT pts[3];
    pts[0].x = x; pts[0].y = y;
    pts[1].x = (int)(x - size * dx + hw * px);
    pts[1].y = (int)(y - size * dy + hw * py);
    pts[2].x = (int)(x - size * dx - hw * px);
    pts[2].y = (int)(y - size * dy - hw * py);
    HPEN pen = CreatePen(PS_SOLID, 1, clr);
    HBRUSH br = filled ? CreateSolidBrush(clr) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);
    if (filled) DeleteObject(br);
}

/* 求中心连线上从矩形边界离开的点（近似裁剪，菱形/圆按比例内缩） */
static void EdgeClip(int* px, int* py, int cx, int cy, int w, int h, double fx, double fy) {
    /* 目标方向 fx,fy 指向对方；从 (px,py)=中心 沿方向走出边界 */
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

/* ============================ 绘制：流程图 ============================ */

static void FlowDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    SetBkMode(hdc, TRANSPARENT);

    /* subgraph 框（先画，垫底） */
    HPEN dashPen = CreatePen(PS_DASH, 1, th->frame);
    HBRUSH subBr = CreateSolidBrush(th->tableHeadBg);
    for (int s = 0; s < d->nSubs; s++) {
        MerSub* sub = &d->subs[s];
        if (!sub->w) continue;
        RECT rc = { ox + sub->x, oy + sub->y, ox + sub->x + sub->w, oy + sub->y + sub->h };
        HBRUSH ob = (HBRUSH)SelectObject(hdc, subBr);
        HPEN op = (HPEN)SelectObject(hdc, dashPen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, UI_Scale(8), UI_Scale(8));
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

    /* 边 */
    HPEN penSolid = CreatePen(PS_SOLID, 1, th->merEdge);
    HPEN penDash  = CreatePen(PS_DASH, 1, th->merEdge);
    HPEN penThick = CreatePen(PS_SOLID, 2, th->merEdge);
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

        if (ed->a == ed->b) {
            /* 自环：顶部小回环 */
            int top = a->y;
            int cx = a->x + a->w / 2;
            POINT pts[4] = {
                { cx - UI_Scale(8), top }, { cx - UI_Scale(8), top - UI_Scale(18) },
                { cx + UI_Scale(8), top - UI_Scale(18) }, { cx + UI_Scale(8), top }
            };
            Polyline(hdc, pts, 4);
            if (ed->arrow)
                DrawArrowHead(hdc, th->merEdge, TRUE, cx + UI_Scale(8), top, 0, 1);
        } else {
            MoveToEx(hdc, x1, y1, NULL);
            LineTo(hdc, x2, y2);
            double ux = fx, uy = fy;   /* 指向 b 的单位向量 */
            {
                double ln2 = sqrt(fx * fx + fy * fy);
                if (ln2 > 0.001) { ux = fx / ln2; uy = fy / ln2; }
            }
            /* 标记端点：markA 时画在 a 端（方向取反） */
            int mx = ed->markA ? x1 : x2, my = ed->markA ? y1 : y2;
            double mdx = ed->markA ? -ux : ux, mdy = ed->markA ? -uy : uy;
            switch (ed->rel) {
                case MRE_INHERIT: {     /* 空心三角 */
                    double px = -mdy, py = mdx;
                    POINT tri[4];
                    tri[0].x = mx; tri[0].y = my;
                    tri[1].x = (int)(mx - UI_Scale(14) * mdx + UI_Scale(7) * px);
                    tri[1].y = (int)(my - UI_Scale(14) * mdy + UI_Scale(7) * py);
                    tri[2].x = (int)(mx - UI_Scale(14) * mdx - UI_Scale(7) * px);
                    tri[2].y = (int)(my - UI_Scale(14) * mdy - UI_Scale(7) * py);
                    tri[3] = tri[0];
                    HBRUSH ob3 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Polygon(hdc, tri, 4);
                    SelectObject(hdc, ob3);
                    break;
                }
                case MRE_COMPOSE:
                case MRE_AGGREG: {      /* 实心/空心菱形 */
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
                        ? CreateSolidBrush(th->merEdge)
                        : (HBRUSH)GetStockObject(NULL_BRUSH);
                    HBRUSH ob3 = (HBRUSH)SelectObject(hdc, db);
                    Polygon(hdc, dm, 5);
                    SelectObject(hdc, ob3);
                    if (ed->rel == MRE_COMPOSE) DeleteObject(db);
                    break;
                }
                case MRE_ASSOC:
                case MRE_DEPEND:
                    DrawArrowHead(hdc, th->merEdge, FALSE, mx, my, mdx, mdy);
                    break;
                default:
                    if (ed->arrow)
                        DrawArrowHead(hdc, th->merEdge, TRUE, x2, y2, fx, fy);
            }
        }
        SelectObject(hdc, op);

        /* 多重性（er/类图两端） */
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

        if (ed->label[0]) {
            int mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
            SIZE sz;
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            GetTextExtentPoint32W(hdc, ed->label, (int)wcslen(ed->label), &sz);
            RECT chip = { mx - sz.cx / 2 - 3, my - sz.cy / 2 - 1,
                          mx + sz.cx / 2 + 3, my + sz.cy / 2 + 1 };
            HBRUSH lb = CreateSolidBrush(th->merLabelBg);
            FillRect(hdc, &chip, lb);
            DeleteObject(lb);
            SetTextColor(hdc, th->merLabelFg);
            SetTextAlign(hdc, TA_CENTER | TA_TOP);
            TextOutW(hdc, mx, chip.top + 1, ed->label, (int)wcslen(ed->label));
            SetTextAlign(hdc, TA_LEFT | TA_TOP);
            SelectObject(hdc, of);
        }
    }
    DeleteObject(penSolid); DeleteObject(penDash); DeleteObject(penThick);

    /* 节点 */
    HPEN nodePen = CreatePen(PS_SOLID, 1, th->merNodeBorder);
    HBRUSH nodeBr = CreateSolidBrush(th->merNodeFill);
    HBRUSH fillBr = CreateSolidBrush(th->merNodeBorder);
    for (int i = 0; i < d->nNodes; i++) {
        MerNode* nd = &d->nodes[i];
        int l = ox + nd->x, t = oy + nd->y, r = l + nd->w, b = t + nd->h;
        HPEN op = (HPEN)SelectObject(hdc, nodePen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, nodeBr);
        switch (nd->shape) {
            case MER_ROUND:
                RoundRect(hdc, l, t, r, b, UI_Scale(12), UI_Scale(12));
                break;
            case MER_STADIUM:
                RoundRect(hdc, l, t, r, b, nd->h / 2, nd->h / 2);
                break;
            case MER_CIRCLE:
                Ellipse(hdc, l, t, r, b);
                break;
            case MER_START: {
                /* 起始：实心圆 */
                int m = UI_Scale(7);
                HBRUSH ob2 = (HBRUSH)SelectObject(hdc, fillBr);
                Ellipse(hdc, l + m, t + m, r - m, b - m);
                SelectObject(hdc, ob2);
                break;
            }
            case MER_END: {
                /* 终止：圆环内实心圆 */
                Ellipse(hdc, l, t, r, b);
                HBRUSH ob2 = (HBRUSH)SelectObject(hdc, fillBr);
                int m = (r - l) / 4;
                Ellipse(hdc, l + m, t + m, r - m, b - m);
                SelectObject(hdc, ob2);
                break;
            }
            case MER_DIAMOND: {
                POINT pts[4] = { { (l + r) / 2, t }, { r, (t + b) / 2 },
                                 { (l + r) / 2, b }, { l, (t + b) / 2 } };
                Polygon(hdc, pts, 4);
                break;
            }
            case MER_FLAG: {
                POINT pts[5] = { { l, t }, { r - UI_Scale(10), t }, { r, (t + b) / 2 },
                                 { r - UI_Scale(10), b }, { l, b } };
                Polygon(hdc, pts, 5);
                break;
            }
            default:
                Rectangle(hdc, l, t, r, b);
        }
        SelectObject(hdc, ob);
        SelectObject(hdc, op);

        SetTextColor(hdc, th->merNodeText);
        if ((d->kind == 3 || d->kind == 4) && nd->extra) {
            /* class / er：三格框（名称 / 属性 / 方法或键） */
            int lineH = FontH(hdc, f->sm) + UI_Scale(2);
            int sep = UI_Scale(1);
            RECT rc0 = { l, t, r, t + lineH + UI_Scale(8) };
            HFONT of = (HFONT)SelectObject(hdc, f->bold);
            DrawTextW(hdc, nd->label, -1, &rc0, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, of);
            int yy = rc0.bottom;
            HPEN op2 = (HPEN)SelectObject(hdc, nodePen);
            MoveToEx(hdc, l, yy, NULL); LineTo(hdc, r, yy);
            wchar_t (*mem)[96] = (wchar_t(*)[96])nd->extra;
            HFONT of2 = (HFONT)SelectObject(hdc, f->sm);
            int total = nd->nExtraA + nd->nExtraB;
            for (int m2 = 0; m2 < total; m2++) {
                if (m2 == nd->nExtraA && d->kind == 3) {   /* class：属性/方法分隔线 */
                    yy += sep;
                    MoveToEx(hdc, l, yy, NULL); LineTo(hdc, r, yy);
                }
                RECT rm = { l + UI_Scale(8), yy, r - UI_Scale(4), yy + lineH };
                SetTextColor(hdc, th->merNodeText);
                TextOutW(hdc, rm.left, yy + UI_Scale(1), mem[m2], (int)wcslen(mem[m2]));
                yy += lineH;
            }
            SelectObject(hdc, of2);
            SelectObject(hdc, op2);
        } else if (nd->shape != MER_START && nd->shape != MER_END) {
            RECT rc = { l + UI_Scale(4), t + UI_Scale(2), r - UI_Scale(4), b - UI_Scale(2) };
            HFONT of = (HFONT)SelectObject(hdc, f->body);
            DrawTextW(hdc, nd->label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, of);
        }
    }
    DeleteObject(nodePen);
    DeleteObject(nodeBr);
    DeleteObject(fillBr);
}

/* ============================ 绘制：时序图 ============================ */

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

    /* 框（loop/alt/opt）：先画在底层。
       由 BEGIN/ELSE/END 配对计算矩形；嵌套缩进。 */
    int indent = 0;
    for (int i = 0; i < d->nItems; i++) {
        MerSeqItem* it = &d->items[i];
        if (it->type == MSI_FRAME_BEGIN) {
            /* 找配对的 END（含同级 ELSE） */
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
            HPEN pen = CreatePen(PS_SOLID, 1, th->frame);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
            RoundRect(hdc, l, t, r, b, UI_Scale(6), UI_Scale(6));
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(pen);

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
            /* 同层 else：分隔线 + 标签（缩进与所在框一致） */
            int l = ox + margin + indent, r = ox + d->sz.cx - margin - indent;
            HPEN pen = CreatePen(PS_DOT, 1, th->frame);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, l, oy + it->y, NULL);
            LineTo(hdc, r, oy + it->y);
            SelectObject(hdc, op);
            DeleteObject(pen);
            wchar_t buf[180];
            _snwprintf(buf, 179, L"else%s%s", it->label[0] ? L" [" : L"", it->label);
            SetTextColor(hdc, th->fgMuted);
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            TextOutW(hdc, l + UI_Scale(8), oy + it->y + UI_Scale(2), buf, (int)wcslen(buf));
            SelectObject(hdc, of);
        }
    }

    /* 生命线 */
    HPEN lifePen = CreatePen(PS_DOT, 1, th->fgMuted);
    HPEN op0 = (HPEN)SelectObject(hdc, lifePen);
    for (int i = 0; i < d->nParts; i++) {
        int x = ox + d->parts[i].x;
        MoveToEx(hdc, x, oy + margin + boxH, NULL);
        LineTo(hdc, x, oy + d->sz.cy - margin);
    }
    SelectObject(hdc, op0);
    DeleteObject(lifePen);

    /* 参与者头 */
    HPEN boxPen = CreatePen(PS_SOLID, 1, th->merNodeBorder);
    HBRUSH boxBr = CreateSolidBrush(th->merNodeFill);
    for (int i = 0; i < d->nParts; i++) {
        int x = ox + d->parts[i].x;
        int w = TextW(hdc, f->bold, d->parts[i].name) + UI_Scale(24);
        if (w < UI_Scale(90)) w = UI_Scale(90);
        RECT rc = { x - w / 2, oy + margin, x + w / 2, oy + margin + boxH };
        HPEN op = (HPEN)SelectObject(hdc, boxPen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, boxBr);
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        SetTextColor(hdc, th->merNodeText);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        DrawTextW(hdc, d->parts[i].name, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, of);
    }
    DeleteObject(boxPen);
    DeleteObject(boxBr);

    /* 消息与备注 */
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
                /* 自发消息：右侧小回勾 */
                int hook = UI_Scale(36);
                POINT pts[3] = { { x1, y - UI_Scale(4) }, { x1 + hook, y - UI_Scale(4) },
                                 { x1 + hook, y + UI_Scale(6) } };
                Polyline(hdc, pts, 3);
                LineTo(hdc, x1 + UI_Scale(2), y + UI_Scale(6));
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
            MoveToEx(hdc, x1, y, NULL);
            LineTo(hdc, x2, y);
            double fx = x2 - x1, fy = 0;
            if (it->head == 2) {
                /* 叉箭头 */
                int s = UI_Scale(5);
                MoveToEx(hdc, x2 - s, y - s, NULL); LineTo(hdc, x2 + s, y + s);
                MoveToEx(hdc, x2 - s, y + s, NULL); LineTo(hdc, x2 + s, y - s);
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
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
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

/* ============================ 对外接口 ============================ */

SIZE Mermaid_Measure(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    if (!d) { SIZE z = { 0, 0 }; return z; }
    if (d->kind == 0 || (d->kind >= 2 && d->kind <= 4)) FlowLayout(d, hdc, f);
    else if (d->kind == 1) SeqLayout(d, hdc, f);
    else ExtLayout(d, hdc, f);
    d->sz.cx += UI_Scale(4);
    d->sz.cy += UI_Scale(4);
    return d->sz;
}

void Mermaid_Draw(MermaidDiagram* d, HDC hdc, int x, int y,
                  const MdFonts* f, const MdTheme* th) {
    if (!d) return;
    if (d->kind == 0 || (d->kind >= 2 && d->kind <= 4)) FlowDraw(d, hdc, x, y, f, th);
    else if (d->kind == 1) SeqDraw(d, hdc, x, y, f, th);
    else ExtDraw(d, hdc, x, y, f, th);
}

#include "mermaid_ext1.inc"
#include "mermaid_ext2.inc"
#include "mermaid_ext3.inc"
