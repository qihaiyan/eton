/* yamlfmt.c — YAML formatter (indent normalizer, pragmatic subset).
   Pure CRT, no globals — independently testable like devtutil.c.
   Supported: block mappings/sequences (dash at same or deeper indent),
   quoted scalars, comments, block scalars (| > with chomp/indent indicators,
   content re-indented verbatim), flow collections spanning lines, anchors/tags
   as opaque text, --- / ... document separators.
   Not rewritten: colon spacing inside values, scalar quoting style. */
#include <string.h>
#include <stdlib.h>

enum { YOK = 0, YERR_MEM = 1 };

typedef struct { char* p; size_t n, cap; } Buf;

static int buf_put(Buf* b, const char* s, size_t n) {
    if (n == 0) return 1;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n) cap *= 2;
        char* p = (char*)realloc(b->p, cap);
        if (!p) return 0;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    return 1;
}
static int buf_ch(Buf* b, char c) { return buf_put(b, &c, 1); }
static int buf_str(Buf* b, const char* s) { return buf_put(b, s, strlen(s)); }

static size_t indent_cols(const char* s, size_t n, size_t* off) {
    size_t col = 0, i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        if (s[i] == '\t') col += 2 - (col % 2);
        else col++;
        i++;
    }
    *off = i;
    return col;
}
static int is_blank(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 0;
    return 1;
}
static int is_docsep(const char* s, size_t off, size_t n) {
    if (off != 0) return 0;
    size_t m = n - off;
    if (m != 3 && !(m > 3 && s[3] == ' ')) return 0;
    return (s[0] == '-' && s[1] == '-' && s[2] == '-') ||
           (s[0] == '.' && s[1] == '.' && s[2] == '.');
}
static int is_seqitem(const char* s, size_t off, size_t n) {
    return off < n && s[off] == '-' &&
           (off + 1 >= n || s[off + 1] == ' ' || s[off + 1] == '\t');
}
static int is_comment(const char* s, size_t off, size_t n) {
    return off < n && s[off] == '#';
}
static int block_opener(const char* s, size_t off, size_t n) {
    if (n <= off) return 0;
    size_t k = n;
    int moved = 0;
    while (k > off && moved < 2 &&
           (s[k - 1] == '+' || s[k - 1] == '-' || (s[k - 1] >= '0' && s[k - 1] <= '9'))) {
        k--;
        moved++;
    }
    if (s[k] != '|' && s[k] != '>') return 0;
    return k == off || s[k - 1] == ' ' || s[k - 1] == '\t';
}
static int flow_scan(const char* s, size_t start, size_t n, int depthIn, int inFlow) {
    int d = depthIn, q = 0;
    for (size_t i = start; i < n; i++) {
        char c = s[i];
        if (q == 1) {
            if (c == '\\') i++;
            else if (c == '"') q = 0;
        } else if (q == 2) {
            if (c == '\'') {
                if (i + 1 < n && s[i + 1] == '\'') i++;
                else q = 0;
            }
        } else if (c == '"') q = 1;
        else if (c == '\'') q = 2;
        else if (c == '#' && !inFlow && (i == start || s[i - 1] == ' ')) break;
        else if (c == '[' || c == '{') d++;
        else if (c == ']' || c == '}') { if (d > 0) d--; }
    }
    return d;
}
static void trim_ws(const char* s, size_t* off, size_t* n) {
    while (*off < *n && (s[*off] == ' ' || s[*off] == '\t')) (*off)++;
    while (*n > *off && (s[*n - 1] == ' ' || s[*n - 1] == '\t' || s[*n - 1] == '\r')) (*n)--;
}

typedef struct { size_t ind; int depth; } Level;
typedef struct { const char* s; size_t ind, off, n; } BLine;
typedef struct { const char* s; size_t off, n; } FLine;

typedef struct {
    Buf b;
    Level* st;
    int nSt, capSt;
    int pendingBlank, emitted;
    BLine* bl;
    int nBl, capBl;
    FLine* fl;
    int nFl, capFl;
    int inBlock, inFlow, flowD, nodeDepth;
    size_t blockBase;
    const char* nl, *ind;
} Ctx;

static int push_level(Ctx* c, size_t ind, int depth) {
    if (c->nSt == c->capSt) {
        int cap = c->capSt ? c->capSt * 2 : 16;
        Level* p = (Level*)realloc(c->st, (size_t)cap * sizeof(Level));
        if (!p) return 0;
        c->st = p;
        c->capSt = cap;
    }
    c->st[c->nSt].ind = ind;
    c->st[c->nSt].depth = depth;
    c->nSt++;
    return 1;
}
static int cur_depth(Ctx* c) { return c->nSt ? c->st[c->nSt - 1].depth : 0; }
static int comment_depth(Ctx* c, size_t col) {
    for (int k = c->nSt - 1; k >= 0; k--)
        if (c->st[k].ind <= col) return c->st[k].depth;
    return 0;
}
static int calc_depth(Ctx* c, size_t col, int* depth) {
    while (c->nSt > 0 && c->st[c->nSt - 1].ind > col) c->nSt--;
    if (c->nSt == 0) {
        if (!push_level(c, col, 0)) return 0;
        *depth = 0;
        return 1;
    }
    if (c->st[c->nSt - 1].ind == col) {
        *depth = c->st[c->nSt - 1].depth;
        return 1;
    }
    *depth = c->st[c->nSt - 1].depth + 1;
    return push_level(c, col, *depth);
}
static int emit_indent(Ctx* c, int depth) {
    for (int k = 0; k < depth; k++)
        if (!buf_str(&c->b, c->ind)) return 0;
    return 1;
}
static int emit_line(Ctx* c, int depth, const char* s, size_t off, size_t n) {
    if (c->emitted && c->pendingBlank && !buf_ch(&c->b, '\n')) return 0;
    c->pendingBlank = 0;
    if (!emit_indent(c, depth)) return 0;
    if (!buf_put(&c->b, s + off, n - off)) return 0;
    if (!buf_str(&c->b, c->nl)) return 0;
    c->emitted = 1;
    return 1;
}
static int emit_blank(Ctx* c) {
    if (c->emitted) c->pendingBlank = 1;
    return 1;
}
static int push_bline(Ctx* c, const char* s, size_t ind, size_t off, size_t n) {
    if (c->nBl == c->capBl) {
        int cap = c->capBl ? c->capBl * 2 : 32;
        BLine* p = (BLine*)realloc(c->bl, (size_t)cap * sizeof(BLine));
        if (!p) return 0;
        c->bl = p;
        c->capBl = cap;
    }
    c->bl[c->nBl].s = s;
    c->bl[c->nBl].ind = ind;
    c->bl[c->nBl].off = off;
    c->bl[c->nBl].n = n;
    c->nBl++;
    return 1;
}
static int push_fline(Ctx* c, const char* s, size_t off, size_t n) {
    if (c->nFl == c->capFl) {
        int cap = c->capFl ? c->capFl * 2 : 32;
        FLine* p = (FLine*)realloc(c->fl, (size_t)cap * sizeof(FLine));
        if (!p) return 0;
        c->fl = p;
        c->capFl = cap;
    }
    c->fl[c->nFl].s = s;
    c->fl[c->nFl].off = off;
    c->fl[c->nFl].n = n;
    c->nFl++;
    return 1;
}
static int flush_block(Ctx* c) {
    size_t minInd = (size_t)-1;
    for (int k = 0; k < c->nBl; k++)
        if (c->bl[k].off < c->bl[k].n && c->bl[k].ind < minInd) minInd = c->bl[k].ind;
    if (minInd == (size_t)-1) minInd = c->blockBase + 1;
    for (int k = 0; k < c->nBl; k++) {
        if (c->bl[k].off >= c->bl[k].n) {
            if (!buf_ch(&c->b, '\n')) return 0;
            continue;
        }
        if (!emit_indent(c, c->nodeDepth + 1)) return 0;
        size_t rel = c->bl[k].ind > minInd ? c->bl[k].ind - minInd : 0;
        for (size_t j = 0; j < rel; j++)
            if (!buf_ch(&c->b, ' ')) return 0;
        if (!buf_put(&c->b, c->bl[k].s + c->bl[k].off, c->bl[k].n - c->bl[k].off)) return 0;
        if (!buf_str(&c->b, c->nl)) return 0;
    }
    c->emitted = 1;
    c->nBl = 0;
    return 1;
}
static int flush_flow(Ctx* c) {
    for (int k = 0; k < c->nFl; k++) {
        size_t off = c->fl[k].off, n = c->fl[k].n;
        trim_ws(c->fl[k].s, &off, &n);
        if (off >= n) continue;
        if (!emit_line(c, c->nodeDepth + 1, c->fl[k].s, off, n)) return 0;
    }
    c->nFl = 0;
    return 1;
}
static int open_block(Ctx* c, size_t col, int depth) {
    c->inBlock = 1;
    c->blockBase = col;
    c->nodeDepth = depth;
    return 1;
}
static int process_line(Ctx* c, const char* L, size_t n) {
    size_t off;
    size_t col = indent_cols(L, n, &off);
    if (is_blank(L, n)) return emit_blank(c);
    if (is_docsep(L, off, n)) {
        c->nSt = 0;
        return emit_line(c, 0, L, off, n);
    }
    if (c->inFlow) {
        c->flowD = flow_scan(L, 0, n, c->flowD, 1);
        if (!push_fline(c, L, off, n)) return 0;
        if (c->flowD == 0) {
            if (!flush_flow(c)) return 0;
            c->inFlow = 0;
        }
        return 1;
    }
    if (is_comment(L, off, n)) return emit_line(c, comment_depth(c, col), L, off, n);
    int depth;
    if (!calc_depth(c, col, &depth)) return 0;
    if (block_opener(L, off, n)) {
        if (!emit_line(c, depth, L, off, n)) return 0;
        return open_block(c, col, depth);
    }
    if (is_seqitem(L, off, n)) {
        size_t coff = off + 1;
        while (coff < n && (L[coff] == ' ' || L[coff] == '\t')) coff++;
        size_t tend = n;
        while (tend > coff && (L[tend - 1] == ' ' || L[tend - 1] == '\t' || L[tend - 1] == '\r')) tend--;
        if (!emit_indent(c, depth)) return 0;
        if (!buf_str(&c->b, "- ")) return 0;
        if (!buf_put(&c->b, L + coff, tend - coff)) return 0;
        if (!buf_str(&c->b, c->nl)) return 0;
        c->emitted = 1;
        c->pendingBlank = 0;
        if (block_opener(L, coff, n)) return open_block(c, col, depth);
        if (flow_scan(L, coff, n, 0, 0) > 0) {
            c->inFlow = 1;
            c->flowD = flow_scan(L, coff, n, 0, 0);
            c->nodeDepth = depth;
        }
        return 1;
    }
    trim_ws(L, &off, &n);
    if (!emit_line(c, depth, L, off, n)) return 0;
    int fd = flow_scan(L, off, n, 0, 0);
    if (fd > 0) {
        c->inFlow = 1;
        c->flowD = fd;
        c->nodeDepth = depth;
    }
    return 1;
}
int Yaml_Reprint(const char* src, size_t len, const char* nl, const char* ind,
                 char** out, size_t* outLen) {
    Ctx c;
    memset(&c, 0, sizeof(c));
    c.nl = nl;
    c.ind = ind;
    size_t i = 0;
    int ok = 1;
    while (i < len && ok) {
        size_t ls = i;
        const char* nlpos = (const char*)memchr(src + i, '\n', len - i);
        size_t le = nlpos ? (size_t)(nlpos - src) : len;
        size_t rawN = le - ls;
        if (rawN > 0 && src[ls + rawN - 1] == '\r') rawN--;
        const char* L = src + ls;
        i = nlpos ? le + 1 : len;
        if (c.inBlock) {
            size_t off;
            size_t col = indent_cols(L, rawN, &off);
            if (is_blank(L, rawN)) {
                if (!push_bline(&c, L, 0, rawN, rawN)) { ok = 0; break; }
            } else if (col > c.blockBase) {
                if (!push_bline(&c, L, col, off, rawN)) { ok = 0; break; }
            } else {
                if (!flush_block(&c)) { ok = 0; break; }
                c.inBlock = 0;
                if (!process_line(&c, L, rawN)) { ok = 0; break; }
            }
            continue;
        }
        if (!process_line(&c, L, rawN)) { ok = 0; break; }
    }
    if (ok && c.inBlock) ok = flush_block(&c);
    if (ok && c.inFlow) ok = flush_flow(&c);
    free(c.st);
    free(c.bl);
    free(c.fl);
    if (!ok) {
        free(c.b.p);
        return YERR_MEM;
    }
    *out = c.b.p;
    *outLen = c.b.n;
    return YOK;
}
