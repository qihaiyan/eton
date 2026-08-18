/* merdagre.c -- 分层布局算法（dagre-d3-es 的 C 移植）
 *
 * 目的：让 eton 的 Mermaid 流程图预览与 mermaid.js（dagre 渲染器）同构。
 * 参考实现：mermaid 12 依赖的 dagre-d3-es 7.0.14，布局流水线为
 *   makeSpaceForEdgeLabels -> removeSelfEdges -> acyclic -> nestingGraph
 *   -> rank(network simplex) -> removeEmptyRanks -> nestingGraph.cleanup
 *   -> normalizeRanks -> assignRankMinMax -> normalize -> order -> position
 *
 * 与 mermaid.c 的约定：本文件只做纯几何计算，不碰 GDI、不碰字体。
 * 调用方把测量好的节点宽高、边、子图归属填进来，layout 之后读回中心坐标。
 */

#include "merdagre.h"

#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MDG_INF 0x7FFFFFFF
#define MDG_MAXITER 4000

static void* mdg_alloc(size_t n) {
    size_t sz = n ? n : 1;
    void* p = malloc(sz);
    if (p) memset(p, 0, sz);
    return p;
}

static void* mdg_realloc(void* p, size_t n) {
    void* q = realloc(p, n ? n : 1);
    return q;
}

/* ---------- 图构造 ---------- */

void mdg_init(MdgGraph* g) {
    memset(g, 0, sizeof(*g));
    g->nodesep = 50;
    g->ranksep = 50;
    g->edgesep = 20;
    g->nodeRankFactor = 1;
    g->nestingRoot = -1;
}

void mdg_free(MdgGraph* g) {
    int i;
    for (i = 0; i < g->nN; i++) {
        free(g->n[i].bl);
        free(g->n[i].br);
    }
    free(g->n);
    free(g->e);
    free(g->chains);
    memset(g, 0, sizeof(*g));
}

int mdg_add_node(MdgGraph* g, int w, int h, int parent) {
    if (g->nN == g->capN) {
        int cap = g->capN ? g->capN * 2 : 32;
        g->n = (MdgNode*)mdg_realloc(g->n, sizeof(MdgNode) * (size_t)cap);
        g->capN = cap;
    }
    {
        MdgNode* p = &g->n[g->nN];
        memset(p, 0, sizeof(*p));
        p->w = w;
        p->h = h;
        p->parent = parent;
        p->type = MDG_REAL;
        p->rank = 0;
        p->borderTop = -1;
        p->borderBottom = -1;
        p->minRank = -1;
        p->maxRank = -1;
        p->origEdge = -1;
        p->alive = 1;
        p->idx = g->nN;
    }
    return g->nN++;
}

static int mdg_add_node_typed(MdgGraph* g, int type, int w, int h, int parent) {
    int i = mdg_add_node(g, w, h, parent);
    g->n[i].type = type;
    return i;
}

static int mdg_add_edge_raw(MdgGraph* g, int u, int v, int weight, int minlen, int nesting) {
    if (g->nE == g->capE) {
        int cap = g->capE ? g->capE * 2 : 64;
        g->e = (MdgEdge*)mdg_realloc(g->e, sizeof(MdgEdge) * (size_t)cap);
        g->capE = cap;
    }
    {
        MdgEdge* e = &g->e[g->nE];
        memset(e, 0, sizeof(*e));
        e->u = u;
        e->v = v;
        e->weight = weight;
        e->minlen = minlen;
        e->nesting = nesting;
        e->origin = g->nE; /* 默认指向自己；acyclic 反向时沿用旧边的 origin */
        e->alive = 1;
    }
    return g->nE++;
}

int mdg_add_edge(MdgGraph* g, int u, int v) {
    return mdg_add_edge_raw(g, u, v, 1, 1, 0);
}

int mdg_add_edge_ex(MdgGraph* g, int u, int v, int weight, int minlen, int nesting) {
    return mdg_add_edge_raw(g, u, v, weight, minlen, nesting);
}

int mdg_node_rank(const MdgGraph* g, int i) {
    if (i < 0 || i >= g->nN) return -1;
    return g->n[i].rank;
}

/* ---------- CSR 邻接（每次重建，只含 alive 边） ---------- */

typedef struct {
    int* off; /* nN+1 */
    int* to;
    int* eid;
    int n;
} MdgAdj;

static void adj_build(MdgAdj* a, const MdgGraph* g, int reverse) {
    int i, nE = 0;
    memset(a, 0, sizeof(*a));
    a->n = g->nN;
    a->off = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN + 1));
    for (i = 0; i < g->nN; i++) a->off[i] = 0;
    for (i = 0; i < g->nE; i++) {
        int src;
        if (!g->e[i].alive) continue;
        src = reverse ? g->e[i].v : g->e[i].u;
        a->off[src + 1]++;
        nE++;
    }
    for (i = 0; i < g->nN; i++) a->off[i + 1] += a->off[i];
    a->to = (int*)mdg_alloc(sizeof(int) * (size_t)(nE > 0 ? nE : 1));
    a->eid = (int*)mdg_alloc(sizeof(int) * (size_t)(nE > 0 ? nE : 1));
    {
        int* cur = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN + 1));
        memcpy(cur, a->off, sizeof(int) * (size_t)(g->nN + 1));
        for (i = 0; i < g->nE; i++) {
            int src, dst, k;
            if (!g->e[i].alive) continue;
            src = reverse ? g->e[i].v : g->e[i].u;
            dst = reverse ? g->e[i].u : g->e[i].v;
            k = cur[src]++;
            a->to[k] = dst;
            a->eid[k] = i;
        }
        free(cur);
    }
}

static void adj_free(MdgAdj* a) {
    free(a->off);
    free(a->to);
    free(a->eid);
    memset(a, 0, sizeof(*a));
}

/* 子节点表：parent 为 -1 的顶层节点（含 nesting 根） */
typedef struct {
    int* off;
    int* list;
    int n;
} MdgChild;

static void child_build(MdgChild* c, const MdgGraph* g) {
    int i, n = 0;
    memset(c, 0, sizeof(*c));
    c->n = g->nN;
    /* slot = parent<0 ? 0 : parent+1，最大 nN，所以 off 需要 nN+2 项 */
    c->off = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN + 2));
    for (i = 0; i <= g->nN + 1; i++) c->off[i] = 0;
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        {
            int p = g->n[i].parent;
            int slot = (p < 0) ? 0 : p + 1;
            c->off[slot + 1]++;
            n++;
        }
    }
    for (i = 0; i <= g->nN; i++) c->off[i + 1] += c->off[i];
    c->list = (int*)mdg_alloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    {
        int* cur = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN + 2));
        memcpy(cur, c->off, sizeof(int) * (size_t)(g->nN + 2));
        for (i = 0; i < g->nN; i++) {
            int p, slot, k;
            if (!g->n[i].alive) continue;
            p = g->n[i].parent;
            slot = (p < 0) ? 0 : p + 1;
            k = cur[slot]++;
            c->list[k] = i;
        }
        free(cur);
    }
}

static void child_free(MdgChild* c) {
    free(c->off);
    free(c->list);
    memset(c, 0, sizeof(*c));
}

/* ---------- 1. acyclic：DFS 回收边，统一反转 ---------- */

static void fas_dfs(const MdgAdj* succ, int v, unsigned char* visited, unsigned char* onstack,
                    int* fas, int* nFas, int cap) {
    int k;
    if (visited[v]) return;
    visited[v] = 1;
    onstack[v] = 1;
    for (k = succ->off[v]; k < succ->off[v + 1]; k++) {
        int w = succ->to[k];
        if (onstack[w]) {
            if (*nFas < cap) fas[(*nFas)++] = succ->eid[k];
        } else {
            fas_dfs(succ, w, visited, onstack, fas, nFas, cap);
        }
    }
    onstack[v] = 0;
}

static void mdg_acyclic(MdgGraph* g) {
    MdgAdj succ;
    unsigned char *visited, *onstack;
    int* fas;
    int nFas = 0, i;

    adj_build(&succ, g, 0);
    visited = (unsigned char*)mdg_alloc((size_t)g->nN);
    onstack = (unsigned char*)mdg_alloc((size_t)g->nN);
    memset(visited, 0, (size_t)g->nN);
    memset(onstack, 0, (size_t)g->nN);
    fas = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nE > 0 ? g->nE : 1));

    for (i = 0; i < g->nN; i++) fas_dfs(&succ, i, visited, onstack, fas, &nFas, g->nE);

    /* dagre 语义：removeEdge + 末尾 setEdge(反向)，边序列位置会变（DFS 顺序依赖它） */
    for (i = 0; i < nFas; i++) {
        int idx = fas[i];
        int u, v, w, ml, ns, org;
        if (idx < 0 || idx >= g->nE) continue;
        u = g->e[idx].u;
        v = g->e[idx].v;
        w = g->e[idx].weight;
        ml = g->e[idx].minlen;
        ns = g->e[idx].nesting;
        org = g->e[idx].origin;
        g->e[idx].alive = 0;
        mdg_add_edge_raw(g, v, u, w, ml, ns);
        g->e[g->nE - 1].reversed = 1;
        g->e[g->nE - 1].origin = org; /* 反向边仍算调用方原来那条边 */
    }

    adj_free(&succ);
    free(visited);
    free(onstack);
    free(fas);
}

/* ---------- 2. nesting-graph ---------- */

static void tree_depths_real(const MdgGraph* g, int v, int depth, int* depths) {
    int k;
    for (k = 0; k < g->nN; k++) {
        if (k == v) continue;
        if (!g->n[k].alive) continue;
        if (g->n[k].parent != v) continue;
        tree_depths_real(g, k, depth + 1, depths);
    }
    if (v >= 0) depths[v] = depth;
}

static int sum_weights(const MdgGraph* g) {
    int i, s = 0;
    for (i = 0; i < g->nE; i++) {
        if (!g->e[i].alive) continue;
        s += g->e[i].weight;
    }
    return s;
}

/* 快照某节点的直接子节点（只含当前已存在、未被 border 污染的） */
static int snap_children(const MdgGraph* g, int v, int* out, int cap) {
    int i, n = 0;
    for (i = 0; i < g->nN && n < cap; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].parent == v) out[n++] = i;
    }
    return n;
}

static void nesting_dfs(MdgGraph* g, int root, int nodeSep, int weight, int height,
                        const int* depths, int v) {
    int kids[64];
    int nCh = snap_children(g, v, kids, 64);
    int k, top, bottom;

    if (nCh <= 0) {
        if (v != root) mdg_add_edge_raw(g, root, v, 0, nodeSep, 1);
        return;
    }

    top = mdg_add_node_typed(g, MDG_BORDER, 0, 0, v);
    bottom = mdg_add_node_typed(g, MDG_BORDER, 0, 0, v);
    g->n[v].borderTop = top;
    g->n[v].borderBottom = bottom;

    for (k = 0; k < nCh; k++) {
        int ch = kids[k];
        int childTop, childBottom, thisWeight, minlen;
        nesting_dfs(g, root, nodeSep, weight, height, depths, ch);

        childTop = (g->n[ch].borderTop >= 0) ? g->n[ch].borderTop : ch;
        childBottom = (g->n[ch].borderBottom >= 0) ? g->n[ch].borderBottom : ch;
        thisWeight = (g->n[ch].borderTop >= 0) ? weight : 2 * weight;
        minlen = (childTop != childBottom) ? 1 : (height - depths[v] + 1);

        mdg_add_edge_raw(g, top, childTop, thisWeight, minlen, 1);
        mdg_add_edge_raw(g, childBottom, bottom, thisWeight, minlen, 1);
    }

    if (g->n[v].parent < 0) {
        mdg_add_edge_raw(g, root, top, 0, height + depths[v], 1);
    }
}

static void mdg_nesting(MdgGraph* g) {
    int root, i, height = 0, nodeSep, weight;
    int* depths;
    int* topList;
    int nTop = 0;

    root = mdg_add_node_typed(g, MDG_ROOT, 0, 0, -1);
    g->nestingRoot = root;

    depths = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    for (i = 0; i < g->nN; i++) depths[i] = 0;

    topList = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].parent < 0) topList[nTop++] = i;
    }
    for (i = 0; i < nTop; i++) tree_depths_real(g, topList[i], 1, depths);

    for (i = 0; i < g->nN; i++)
        if (depths[i] > height) height = depths[i];
    height -= 1;

    g->nodeRankFactor = 2 * height + 1;
    nodeSep = 2 * height + 1;

    for (i = 0; i < g->nE; i++) {
        if (!g->e[i].alive) continue;
        g->e[i].minlen *= nodeSep;
    }
    weight = sum_weights(g) + 1;

    for (i = 0; i < nTop; i++) {
        int v = topList[i];
        if (v == root) continue;
        nesting_dfs(g, root, nodeSep, weight, height, depths, v);
    }

    free(topList);
    free(depths);
}

static void mdg_nesting_cleanup(MdgGraph* g) {
    int i;
    if (g->nestingRoot >= 0) g->n[g->nestingRoot].alive = 0;
    for (i = 0; i < g->nE; i++) {
        if (g->e[i].nesting) g->e[i].alive = 0;
    }
}

/* ---------- 3. rank：network simplex ---------- */

/* simplify：只保留非子图节点，多重边合并（weight 相加，minlen 取大） */
typedef struct {
    int* eu;
    int* ev;
    int* ew;
    int* emin;
    int ne;
    unsigned char* active; /* 节点是否参与 */
    int nActive;
} MdgSimp;

static void simp_build(MdgSimp* s, const MdgGraph* g) {
    int i, j;
    memset(s, 0, sizeof(*s));
    s->eu = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nE > 0 ? g->nE : 1));
    s->ev = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nE > 0 ? g->nE : 1));
    s->ew = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nE > 0 ? g->nE : 1));
    s->emin = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nE > 0 ? g->nE : 1));
    s->active = (unsigned char*)mdg_alloc((size_t)g->nN);
    memset(s->active, 0, (size_t)g->nN);
    s->nActive = 0;
    for (i = 0; i < g->nN; i++) {
        int hasChild = 0, k;
        for (k = 0; k < g->nN; k++) {
            if (g->n[k].alive && g->n[k].parent == i) {
                hasChild = 1;
                break;
            }
        }
        if (g->n[i].alive && !hasChild) {
            s->active[i] = 1;
            s->nActive++;
        }
    }
    for (i = 0; i < g->nE; i++) {
        int u, v, found = -1;
        if (!g->e[i].alive) continue;
        u = g->e[i].u;
        v = g->e[i].v;
        if (!s->active[u] || !s->active[v]) continue;
        for (j = 0; j < s->ne; j++) {
            if (s->eu[j] == u && s->ev[j] == v) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            s->ew[found] += g->e[i].weight;
            if (g->e[i].minlen > s->emin[found]) s->emin[found] = g->e[i].minlen;
        } else {
            s->eu[s->ne] = u;
            s->ev[s->ne] = v;
            s->ew[s->ne] = g->e[i].weight;
            s->emin[s->ne] = g->e[i].minlen;
            s->ne++;
        }
    }
}

static void simp_free(MdgSimp* s) {
    free(s->eu);
    free(s->ev);
    free(s->ew);
    free(s->emin);
    free(s->active);
    memset(s, 0, sizeof(*s));
}

typedef struct {
    MdgSimp* s;
    MdgAdj out;
    MdgAdj in;
    int* rank;
    unsigned char* visited;
} RankCtx;

/* longestPath：按拓扑逆序求 rank[v] = min(rank[w] - minlen)，无出边为 0。
 * 用迭代实现，避免递归在意外环上爆栈。 */
static void longest_path(RankCtx* x, int nN) {
    int* indeg = (int*)mdg_alloc(sizeof(int) * (size_t)nN);
    int* order = (int*)mdg_alloc(sizeof(int) * (size_t)nN);
    int qh = 0, qt = 0, i, k;

    for (i = 0; i < nN; i++) {
        x->rank[i] = 0;
        indeg[i] = x->s->active[i] ? (x->in.off[i + 1] - x->in.off[i]) : 0;
    }
    for (i = 0; i < nN; i++)
        if (x->s->active[i] && indeg[i] == 0) order[qt++] = i;

    while (qh < qt) {
        int v = order[qh++];
        for (k = x->out.off[v]; k < x->out.off[v + 1]; k++) {
            int e = x->out.eid[k];
            int w = x->s->ev[e];
            if (--indeg[w] == 0) order[qt++] = w;
        }
    }
    for (i = 0; i < nN; i++) {
        if (x->s->active[i] && indeg[i] > 0) order[qt++] = i;
    }

    for (i = qt - 1; i >= 0; i--) {
        int v = order[i];
        int has = 0, best = MDG_INF;
        for (k = x->out.off[v]; k < x->out.off[v + 1]; k++) {
            int e = x->out.eid[k];
            int r = x->rank[x->s->ev[e]] - x->s->emin[e];
            if (!has || r < best) {
                best = r;
                has = 1;
            }
        }
        x->rank[v] = has ? best : 0;
    }

    free(indeg);
    free(order);
}

/* 紧致树 */
typedef struct {
    int u, v;
    int cut;
    int alive;
} TEdge;

typedef struct {
    int n;
    unsigned char* in;
    int* order;
    int nOrder;
    TEdge* te;
    int nt, capt;
} Tree;

static void tree_init(Tree* t, int n, int start) {
    memset(t, 0, sizeof(*t));
    t->n = n;
    t->in = (unsigned char*)mdg_alloc((size_t)n);
    memset(t->in, 0, (size_t)n);
    t->order = (int*)mdg_alloc(sizeof(int) * (size_t)n);
    t->nOrder = 0;
    t->te = (TEdge*)mdg_alloc(sizeof(TEdge) * (size_t)(n * 2 + 4));
    t->nt = 0;
    t->capt = n * 2 + 4;
    t->in[start] = 1;
    t->order[t->nOrder++] = start;
}

static void tree_free(Tree* t) {
    free(t->in);
    free(t->order);
    free(t->te);
    memset(t, 0, sizeof(*t));
}

static void tree_add_node(Tree* t, int v) {
    if (!t->in[v]) {
        t->in[v] = 1;
        t->order[t->nOrder++] = v;
    }
}

static int tree_has_edge(const Tree* t, int u, int v) {
    int i;
    for (i = 0; i < t->nt; i++) {
        if (!t->te[i].alive) continue;
        if ((t->te[i].u == u && t->te[i].v == v) || (t->te[i].u == v && t->te[i].v == u))
            return i;
    }
    return -1;
}

static void tree_add_edge(Tree* t, int u, int v) {
    if (t->nt == t->capt) {
        int cap = t->capt * 2;
        t->te = (TEdge*)mdg_realloc(t->te, sizeof(TEdge) * (size_t)cap);
        t->capt = cap;
    }
    t->te[t->nt].u = u;
    t->te[t->nt].v = v;
    t->te[t->nt].cut = 0;
    t->te[t->nt].alive = 1;
    t->nt++;
}

static int tree_count(const Tree* t) {
    int i, c = 0;
    for (i = 0; i < t->n; i++)
        if (t->in[i]) c++;
    return c;
}

static int g_slack(const MdgSimp* s, int e, const int* rank) {
    return rank[s->ev[e]] - rank[s->eu[e]] - s->emin[e];
}

/* 邻接：某节点在 simplify 图里的所有入射边 */
static void node_edges(const MdgSimp* s, int v, int* out, int* n, int cap) {
    int i;
    *n = 0;
    for (i = 0; i < s->ne; i++) {
        if (s->eu[i] == v || s->ev[i] == v) {
            if (*n < cap) out[(*n)++] = i;
        }
    }
}

static void tight_tree_dfs(Tree* t, RankCtx* x, int v) {
    int i;
    for (i = 0; i < x->s->ne; i++) {
        int w;
        if (x->s->eu[i] != v && x->s->ev[i] != v) continue;
        w = (x->s->eu[i] == v) ? x->s->ev[i] : x->s->eu[i];
        if (t->in[w]) continue;
        if (g_slack(x->s, i, x->rank) != 0) continue;
        tree_add_node(t, w);
        tree_add_edge(t, v, w);
        tight_tree_dfs(t, x, w);
    }
}

static int tight_tree(Tree* t, RankCtx* x) {
    int i;
    int snap = t->nOrder;
    int* list = (int*)mdg_alloc(sizeof(int) * (size_t)(snap > 0 ? snap : 1));
    memcpy(list, t->order, sizeof(int) * (size_t)snap);
    for (i = 0; i < snap; i++) tight_tree_dfs(t, x, list[i]);
    free(list);
    return tree_count(t);
}

static int find_min_slack_edge(const Tree* t, const MdgSimp* s, const int* rank) {
    int i, best = -1, bestSlack = MDG_INF;
    for (i = 0; i < s->ne; i++) {
        int inU = t->in[s->eu[i]], inV = t->in[s->ev[i]];
        if (inU == inV) continue;
        {
            int sl = g_slack(s, i, rank);
            if (best < 0 || sl < bestSlack) {
                best = i;
                bestSlack = sl;
            }
        }
    }
    return best;
}

static void shift_ranks(const Tree* t, int* rank, int delta) {
    int i;
    for (i = 0; i < t->n; i++)
        if (t->in[i]) rank[i] += delta;
}

static void feasible_tree(Tree* t, RankCtx* x, int start) {
    int size = x->s->nActive;
    int guard = 0;
    while (tight_tree(t, x) < size && guard++ < 10000) {
        int e = find_min_slack_edge(t, x->s, x->rank);
        int delta;
        if (e < 0) break;
        delta = t->in[x->s->eu[e]] ? g_slack(x->s, e, x->rank)
                                   : -g_slack(x->s, e, x->rank);
        if (delta == 0) delta = 1;
        shift_ranks(t, x->rank, delta);
    }
}

/* low / lim / tree parent */
static void dfs_low_lim(Tree* t, int v, int parent, int* nextLim, int* low, int* lim, int* par,
                        unsigned char* vis) {
    int i;
    int lo = *nextLim;
    vis[v] = 1;
    for (i = 0; i < t->nt; i++) {
        int w;
        if (!t->te[i].alive) continue;
        if (t->te[i].u == v) w = t->te[i].v;
        else if (t->te[i].v == v) w = t->te[i].u;
        else continue;
        if (!vis[w]) dfs_low_lim(t, w, v, nextLim, low, lim, par, vis);
    }
    low[v] = lo;
    lim[v] = (*nextLim)++;
    par[v] = parent;
}

static void init_low_lim(Tree* t, int* low, int* lim, int* par) {
    unsigned char* vis = (unsigned char*)mdg_alloc((size_t)t->n);
    int nextLim = 1;
    int i;
    memset(vis, 0, (size_t)t->n);
    for (i = 0; i < t->n; i++) {
        low[i] = 0;
        lim[i] = 0;
        par[i] = -1;
    }
    for (i = 0; i < t->nOrder; i++) {
        int v = t->order[i];
        if (!vis[v]) dfs_low_lim(t, v, -1, &nextLim, low, lim, par, vis);
    }
    free(vis);
}

static int calc_cut_value(Tree* t, RankCtx* x, int child, const int* par) {
    int parent = par[child];
    int childIsTail = 1;
    int ge = -1, cutValue, i;
    for (i = 0; i < x->s->ne; i++) {
        if (x->s->eu[i] == child && x->s->ev[i] == parent) {
            ge = i;
            break;
        }
    }
    if (ge < 0) {
        childIsTail = 0;
        for (i = 0; i < x->s->ne; i++) {
            if (x->s->eu[i] == parent && x->s->ev[i] == child) {
                ge = i;
                break;
            }
        }
    }
    if (ge < 0) return 0;
    cutValue = x->s->ew[ge];

    for (i = 0; i < x->s->ne; i++) {
        int isOut, other, pointsToHead, otherWeight;
        if (x->s->eu[i] != child && x->s->ev[i] != child) continue;
        isOut = (x->s->eu[i] == child);
        other = isOut ? x->s->ev[i] : x->s->eu[i];
        if (other == parent) continue;
        pointsToHead = (isOut == childIsTail);
        otherWeight = x->s->ew[i];
        cutValue += pointsToHead ? otherWeight : -otherWeight;
        {
            int ti = tree_has_edge(t, child, other);
            if (ti >= 0) {
                int ocv = t->te[ti].cut;
                cutValue += pointsToHead ? -ocv : ocv;
            }
        }
    }
    return cutValue;
}

static void post_dfs(Tree* t, int v, unsigned char* vis, int* out, int* n, const int* par) {
    int i;
    if (vis[v]) return;
    vis[v] = 1;
    for (i = 0; i < t->nt; i++) {
        int w;
        if (!t->te[i].alive) continue;
        if (t->te[i].u == v) w = t->te[i].v;
        else if (t->te[i].v == v) w = t->te[i].u;
        else continue;
        post_dfs(t, w, vis, out, n, par);
    }
    out[(*n)++] = v;
}

static void init_cut_values(Tree* t, RankCtx* x, const int* par) {
    unsigned char* vis = (unsigned char*)mdg_alloc((size_t)t->n);
    int* post = (int*)mdg_alloc(sizeof(int) * (size_t)t->n);
    int np = 0, i;
    memset(vis, 0, (size_t)t->n);
    for (i = 0; i < t->nOrder; i++) post_dfs(t, t->order[i], vis, post, &np, par);
    for (i = 0; i + 1 < np; i++) {
        int child = post[i];
        int parent = par[child];
        int ti = tree_has_edge(t, child, parent);
        if (ti >= 0) t->te[ti].cut = calc_cut_value(t, x, child, par);
    }
    free(vis);
    free(post);
}

static int leave_edge(const Tree* t) {
    int i;
    for (i = 0; i < t->nt; i++) {
        if (!t->te[i].alive) continue;
        if (t->te[i].cut < 0) return i;
    }
    return -1;
}

static int is_descendant(int vLim, const int* low, const int* lim, int rootNode) {
    return low[rootNode] <= lim[vLim] && lim[vLim] <= lim[rootNode];
}

static int enter_edge(Tree* t, RankCtx* x, int te, const int* low, const int* lim) {
    int v = t->te[te].u;
    int w = t->te[te].v;
    int tail, flip = 0, best = -1, bestSlack = MDG_INF, i;
    int edgeV, edgeW;

    /* 找到图里存在的方向 */
    edgeV = -1;
    for (i = 0; i < x->s->ne; i++) {
        if (x->s->eu[i] == v && x->s->ev[i] == w) {
            edgeV = v;
            edgeW = w;
            break;
        }
    }
    if (edgeV < 0) {
        edgeV = w;
        edgeW = v;
    }
    tail = edgeV;
    if (lim[edgeV] > lim[edgeW]) {
        tail = edgeW;
        flip = 1;
    }

    for (i = 0; i < x->s->ne; i++) {
        int cond1 = (flip == is_descendant(x->s->eu[i], low, lim, tail));
        int cond2 = (flip != is_descendant(x->s->ev[i], low, lim, tail));
        int sl;
        if (!(cond1 && cond2)) continue;
        sl = g_slack(x->s, i, x->rank);
        if (best < 0 || sl < bestSlack) {
            best = i;
            bestSlack = sl;
        }
    }
    return best;
}

static void pre_dfs(Tree* t, int v, unsigned char* vis, int* out, int* n) {
    int i;
    if (vis[v]) return;
    vis[v] = 1;
    out[(*n)++] = v;
    for (i = 0; i < t->nt; i++) {
        int w;
        if (!t->te[i].alive) continue;
        if (t->te[i].u == v) w = t->te[i].v;
        else if (t->te[i].v == v) w = t->te[i].u;
        else continue;
        pre_dfs(t, w, vis, out, n);
    }
}

static void update_ranks(Tree* t, RankCtx* x, const int* par) {
    int root = -1, i, n = 0;
    unsigned char* vis = (unsigned char*)mdg_alloc((size_t)t->n);
    int* pre = (int*)mdg_alloc(sizeof(int) * (size_t)t->n);

    for (i = 0; i < t->nOrder; i++) {
        if (t->in[t->order[i]] && par[t->order[i]] < 0) {
            root = t->order[i];
            break;
        }
    }
    if (root < 0) {
        free(vis);
        free(pre);
        return;
    }
    memset(vis, 0, (size_t)t->n);
    pre_dfs(t, root, vis, pre, &n);
    for (i = 1; i < n; i++) {
        int v = pre[i];
        int parent = par[v];
        int ge = -1, flipped = 0;
        for (ge = 0; ge < x->s->ne; ge++)
            if (x->s->eu[ge] == v && x->s->ev[ge] == parent) break;
        if (ge >= x->s->ne) {
            for (ge = 0; ge < x->s->ne; ge++)
                if (x->s->eu[ge] == parent && x->s->ev[ge] == v) break;
            flipped = 1;
        }
        if (ge >= x->s->ne) continue;
        x->rank[v] = x->rank[parent] + (flipped ? x->s->emin[ge] : -x->s->emin[ge]);
    }
    free(vis);
    free(pre);
}

static void exchange_edges(Tree* t, RankCtx* x, int te, int fe, int* low, int* lim, int* par) {
    int ti;
    t->te[te].alive = 0;
    tree_add_edge(t, x->s->eu[fe], x->s->ev[fe]);
    init_low_lim(t, low, lim, par);
    init_cut_values(t, x, par);
    update_ranks(t, x, par);
    (void)ti;
}

static void mdg_rank_simplex(MdgGraph* g) {
    MdgSimp s;
    RankCtx x;
    Tree t;
    int* low;
    int* lim;
    int* par;
    int i, guard = 0;

    simp_build(&s, g);
    if (s.nActive == 0) {
        simp_free(&s);
        return;
    }

    memset(&x, 0, sizeof(x));
    x.s = &s;
    x.rank = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    x.visited = (unsigned char*)mdg_alloc((size_t)g->nN);
    for (i = 0; i < g->nN; i++) x.rank[i] = 0;

    {
        /* 为 simplify 图建出/入邻接 */
        MdgGraph tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.nN = g->nN;
        tmp.nE = s.ne;
        tmp.e = (MdgEdge*)mdg_alloc(sizeof(MdgEdge) * (size_t)(s.ne > 0 ? s.ne : 1));
        for (i = 0; i < s.ne; i++) {
            tmp.e[i].u = s.eu[i];
            tmp.e[i].v = s.ev[i];
            tmp.e[i].alive = 1;
            tmp.e[i].weight = s.ew[i];
            tmp.e[i].minlen = s.emin[i];
        }
        adj_build(&x.out, &tmp, 0);
        adj_build(&x.in, &tmp, 1);
        free(tmp.e);
    }

    longest_path(&x, g->nN);

    /* 起点：第一个活跃节点 */
    {
        int start = 0;
        for (i = 0; i < g->nN; i++)
            if (s.active[i]) {
                start = i;
                break;
            }
        tree_init(&t, g->nN, start);
    }

    feasible_tree(&t, &x, 0);

    low = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    lim = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    par = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    init_low_lim(&t, low, lim, par);
    init_cut_values(&t, &x, par);

    for (;;) {
        int te = leave_edge(&t);
        int fe;
        if (te < 0) break;
        if (guard++ > MDG_MAXITER) break;
        fe = enter_edge(&t, &x, te, low, lim);
        if (fe < 0) {
            t.te[te].cut = 0;
            continue;
        }
        exchange_edges(&t, &x, te, fe, low, lim, par);
    }

    for (i = 0; i < g->nN; i++) g->n[i].rank = x.rank[i];

    free(low);
    free(lim);
    free(par);
    tree_free(&t);
    adj_free(&x.out);
    adj_free(&x.in);
    free(x.rank);
    free(x.visited);
    simp_free(&s);
}

/* ---------- 4. removeEmptyRanks / normalizeRanks ---------- */

static void remove_empty_ranks(MdgGraph* g) {
    int minR = MDG_INF, maxR = -MDG_INF, i, factor = g->nodeRankFactor;
    int* hasLayer;
    int delta = 0, span;

    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].type == MDG_SUB) continue; /* 子图无 rank，不占层 */
        if (g->n[i].rank < minR) minR = g->n[i].rank;
        if (g->n[i].rank > maxR) maxR = g->n[i].rank;
    }
    if (minR > maxR) return;
    span = maxR - minR + 1;
    hasLayer = (int*)mdg_alloc(sizeof(int) * (size_t)span);
    for (i = 0; i < span; i++) hasLayer[i] = 0;
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].type == MDG_SUB) continue;
        hasLayer[g->n[i].rank - minR] = 1;
    }
    for (i = 0; i < span; i++) {
        if (!hasLayer[i]) {
            if (factor <= 0 || (i % factor) != 0) delta--;
        } else if (delta) {
            int k;
            for (k = 0; k < g->nN; k++) {
                if (!g->n[k].alive) continue;
                if (g->n[k].type == MDG_SUB) continue;
                if (g->n[k].rank - minR == i) g->n[k].rank += delta;
            }
        }
    }
    free(hasLayer);
}

static void normalize_ranks(MdgGraph* g) {
    int minR = MDG_INF, i;
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].type == MDG_SUB) continue;
        if (g->n[i].rank < minR) minR = g->n[i].rank;
    }
    if (minR == MDG_INF) return;
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].type == MDG_SUB) continue;
        g->n[i].rank -= minR;
    }
}

static void assign_rank_minmax(MdgGraph* g) {
    int i, maxR = 0;
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].borderTop >= 0) {
            g->n[i].minRank = g->n[g->n[i].borderTop].rank;
            g->n[i].maxRank = g->n[g->n[i].borderBottom].rank;
            if (g->n[i].maxRank > maxR) maxR = g->n[i].maxRank;
        }
    }
    g->maxRank = maxR;
}

/* ---------- 5. normalize：长边打断成每层一段 ---------- */

static void mdg_normalize(MdgGraph* g) {
    int nOrig = g->nE, ei;
    for (ei = 0; ei < nOrig; ei++) {
        int v, w, rv, rw, r, prev, wt;
        if (!g->e[ei].alive) continue;
        v = g->e[ei].u;
        w = g->e[ei].v;
        wt = g->e[ei].weight;
        rv = g->n[v].rank;
        rw = g->n[w].rank;
        if (rw <= rv + 1) continue;
        if (rw - rv > 300) continue;
        g->e[ei].alive = 0;
        prev = v;
        for (r = rv + 1; r < rw; r++) {
            int d = mdg_add_node_typed(g, MDG_EDGE, 0, 0, -1);
            g->n[d].rank = r;
            g->n[d].origEdge = ei;
            if (r == rv + 1) {
                if (g->nChains >= g->capChains) {
                    int cap = g->capChains ? g->capChains * 2 : 16;
                    g->chains = (int*)mdg_realloc(g->chains, sizeof(int) * (size_t)cap);
                    g->capChains = cap;
                }
                g->chains[g->nChains++] = d;
            }
            mdg_add_edge_raw(g, prev, d, wt, 1, 0);
            prev = d;
        }
        mdg_add_edge_raw(g, prev, w, wt, 1, 0);
    }
}

/* ---------- 6. parentDummyChains：把归一化 dummy 挂到合适的子图 ---------- */

static void pdc_post_dfs(const MdgGraph* g, int v, int* low, int* lim, int* cur) {
    int k;
    low[v] = *cur;
    for (k = 0; k < g->nN; k++) {
        if (!g->n[k].alive) continue;
        if (g->n[k].parent != v) continue;
        pdc_post_dfs(g, k, low, lim, cur);
    }
    lim[v] = (*cur)++;
}

static int chain_next(const MdgGraph* g, int v) {
    int e;
    for (e = 0; e < g->nE; e++) {
        if (!g->e[e].alive) continue;
        if (g->e[e].u == v) return g->e[e].v;
    }
    return -1;
}

static void parent_dummy_chains(MdgGraph* g) {
    int* low;
    int* lim;
    int cur = 0, c, i;
    if (g->nChains <= 0) return;
    low = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    lim = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].parent >= 0) continue;
        pdc_post_dfs(g, i, low, lim, &cur);
    }

    for (c = 0; c < g->nChains; c++) {
        int v0 = g->chains[c];
        int ei, src, dst, lo, hi, par, lca, pathV;
        int vPath[32], nvp = 0, wPath[32], nwp = 0, path[64], np = 0;
        int pathIdx = 0, ascending = 1, v, guard = 0;
        if (v0 < 0 || v0 >= g->nN) continue;
        ei = g->n[v0].origEdge;
        if (ei < 0) continue;
        src = g->e[ei].u;
        dst = g->e[ei].v;
        if (src < 0 || dst < 0) continue;

        lo = low[src] < low[dst] ? low[src] : low[dst];
        hi = lim[src] > lim[dst] ? lim[src] : lim[dst];

        par = src;
        do {
            par = g->n[par].parent;
            if (nvp < 32) vPath[nvp++] = par;
        } while (par >= 0 && (low[par] > lo || hi > lim[par]));
        lca = par;

        par = dst;
        while (nwp < 32) {
            par = (par >= 0) ? g->n[par].parent : -1;
            if (par == lca) break;
            if (par < 0) break;
            wPath[nwp++] = par;
        }

        for (i = 0; i < nvp && np < 64; i++) path[np++] = vPath[i];
        for (i = nwp - 1; i >= 0 && np < 64; i--) path[np++] = wPath[i];
        if (np <= 0) continue;

        v = v0;
        while (v >= 0 && v != dst && guard++ < 4096) {
            int rank = g->n[v].rank;
            if (ascending) {
                for (;;) {
                    pathV = (pathIdx < np) ? path[pathIdx] : -1;
                    if (pathV == lca || pathV < 0) break;
                    if (!(g->n[pathV].maxRank < rank)) break;
                    pathIdx++;
                }
                if (pathV == lca) ascending = 0;
            }
            if (!ascending) {
                while (pathIdx < np - 1) {
                    int nx = path[pathIdx + 1];
                    if (nx < 0) break;
                    if (!(g->n[nx].minRank <= rank)) break;
                    pathIdx++;
                }
                pathV = (pathIdx < np) ? path[pathIdx] : -1;
            }
            g->n[v].parent = pathV;
            v = chain_next(g, v);
        }
    }

    free(low);
    free(lim);
}

/* ---------- 7. addBorderSegments：每个子图每层左右各一个边框节点 ---------- */

static void abs_dfs(MdgGraph* g, int v) {
    int kids[64];
    int nk = 0, k, span, r;
    for (k = 0; k < g->nN && nk < 64; k++) {
        if (!g->n[k].alive) continue;
        if (g->n[k].parent != v) continue;
        kids[nk++] = k;
    }
    for (k = 0; k < nk; k++) abs_dfs(g, kids[k]);

    if (v < 0 || v >= g->nN) return;
    if (g->n[v].borderTop < 0) return;
    span = g->n[v].maxRank - g->n[v].minRank + 1;
    if (span <= 0) return;
    g->n[v].bl = (int*)mdg_alloc(sizeof(int) * (size_t)span);
    g->n[v].br = (int*)mdg_alloc(sizeof(int) * (size_t)span);
    for (r = 0; r < span; r++) {
        g->n[v].bl[r] = -1;
        g->n[v].br[r] = -1;
    }
    for (r = 0; r < span; r++) {
        int rr = g->n[v].minRank + r;
        int l = mdg_add_node_typed(g, MDG_BORDER, 0, 0, v);
        int rn;
        g->n[l].rank = rr;
        g->n[l].borderType = 1;
        g->n[v].bl[r] = l;
        if (r > 0) mdg_add_edge_raw(g, g->n[v].bl[r - 1], l, 1, 1, 0);
        rn = mdg_add_node_typed(g, MDG_BORDER, 0, 0, v);
        g->n[rn].rank = rr;
        g->n[rn].borderType = 2;
        g->n[v].br[r] = rn;
        if (r > 0) mdg_add_edge_raw(g, g->n[v].br[r - 1], rn, 1, 1, 0);
    }
}

static void add_border_segments(MdgGraph* g) {
    int* tops;
    int n = 0, i;
    if (g->nN <= 0) return;
    tops = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        if (g->n[i].parent >= 0) continue;
        tops[n++] = i;
    }
    for (i = 0; i < n; i++) abs_dfs(g, tops[i]);
    free(tops);
}

/* ---------- 8. order：层图 + 重心 + 约束合并（对齐 dagre） ---------- */

typedef struct {
    int* nodes; /* 按层连续存放 */
    int* start; /* nRanks+1 */
    int* ord;   /* 节点 -> 层内序号 */
    int nRanks;
    int n;
} Layering;

static int is_dummy(const MdgGraph* g, int v) {
    return g->n[v].type == MDG_EDGE || g->n[v].type == MDG_BORDER;
}

static void layering_free(Layering* L) {
    free(L->nodes);
    free(L->start);
    free(L->ord);
    memset(L, 0, sizeof(*L));
}

static void layering_from_order(Layering* L, const MdgGraph* g) {
    int i, r, maxR = -1, n = 0;
    int* cur;
    memset(L, 0, sizeof(*L));
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        if (g->n[i].rank > maxR) maxR = g->n[i].rank;
        n++;
    }
    L->nRanks = maxR + 1;
    L->n = n;
    if (maxR < 0) {
        L->nodes = (int*)mdg_alloc(sizeof(int));
        L->start = (int*)mdg_alloc(sizeof(int));
        L->ord = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN > 0 ? g->nN : 1));
        return;
    }
    L->start = (int*)mdg_alloc(sizeof(int) * (size_t)(L->nRanks + 1));
    L->nodes = (int*)mdg_alloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    L->ord = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN > 0 ? g->nN : 1));
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        L->start[g->n[i].rank + 1]++;
    }
    for (r = 0; r < L->nRanks; r++) L->start[r + 1] += L->start[r];
    cur = (int*)mdg_alloc(sizeof(int) * (size_t)(L->nRanks + 1));
    memcpy(cur, L->start, sizeof(int) * (size_t)(L->nRanks + 1));
    for (i = 0; i < g->nN; i++) {
        int slot, r;
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        r = g->n[i].rank;
        slot = L->start[r] + g->n[i].order;
        if (g->n[i].order < 0 || slot >= L->start[r + 1]) slot = cur[r]++;
        else cur[r] = slot + 1;
        L->nodes[slot] = i;
        L->ord[i] = slot - L->start[r];
    }
    free(cur);
}

/* 层图：某个 rank 上可移动节点 + 层级关系 + 入边（用于重心）*/
typedef struct {
    int root;
    int rank;
    int n;
    int* parent;
    int* chead;
    int* ctail;
    int* cnext;
    unsigned char* pres;
    int* eu;
    int* ev;
    int* ew;
    int ne, capE;
    int* ieS;
    int* ieW;
    int* ieNext;
    int* inHead;
    int ieN, capIE;
} LGraph;

static void lg_init(LGraph* lg, int nN, int rank) {
    int n = nN + 1;
    memset(lg, 0, sizeof(*lg));
    lg->n = n;
    lg->root = nN;
    lg->rank = rank;
    lg->parent = (int*)mdg_alloc(sizeof(int) * (size_t)n);
    lg->chead = (int*)mdg_alloc(sizeof(int) * (size_t)n);
    lg->ctail = (int*)mdg_alloc(sizeof(int) * (size_t)n);
    lg->cnext = (int*)mdg_alloc(sizeof(int) * (size_t)n);
    lg->pres = (unsigned char*)mdg_alloc((size_t)n);
    lg->inHead = (int*)mdg_alloc(sizeof(int) * (size_t)n);
    {
        int i;
        for (i = 0; i < n; i++) {
            lg->parent[i] = -1;
            lg->chead[i] = -1;
            lg->ctail[i] = -1;
            lg->cnext[i] = -1;
            lg->inHead[i] = -1;
        }
    }
}

static void lg_free(LGraph* lg) {
    free(lg->parent);
    free(lg->chead);
    free(lg->ctail);
    free(lg->cnext);
    free(lg->pres);
    free(lg->eu);
    free(lg->ev);
    free(lg->ew);
    free(lg->ieS);
    free(lg->ieW);
    free(lg->ieNext);
    free(lg->inHead);
    memset(lg, 0, sizeof(*lg));
}

static void lg_touch(LGraph* lg, int v) {
    if (v < 0 || v >= lg->n) return;
    if (lg->pres[v]) return;
    lg->pres[v] = 1;
    lg->parent[v] = -1;
    lg->chead[v] = -1;
    lg->ctail[v] = -1;
    lg->cnext[v] = -1;
}

static void lg_set_parent(LGraph* lg, int v, int p) {
    if (lg->parent[v] == p) return;
    lg->parent[v] = p;
    lg->cnext[v] = -1;
    if (lg->ctail[p] < 0) lg->chead[p] = v;
    else lg->cnext[lg->ctail[p]] = v;
    lg->ctail[p] = v;
}

static void lg_add_edge(LGraph* lg, int u, int v, int w) {
    int i;
    if (u == v) return;
    for (i = 0; i < lg->ne; i++) {
        if (lg->eu[i] == u && lg->ev[i] == v) {
            lg->ew[i] += w;
            return;
        }
    }
    if (lg->ne >= lg->capE) {
        int cap = lg->capE ? lg->capE * 2 : 32;
        lg->eu = (int*)mdg_realloc(lg->eu, sizeof(int) * (size_t)cap);
        lg->ev = (int*)mdg_realloc(lg->ev, sizeof(int) * (size_t)cap);
        lg->ew = (int*)mdg_realloc(lg->ew, sizeof(int) * (size_t)cap);
        lg->capE = cap;
    }
    lg->eu[lg->ne] = u;
    lg->ev[lg->ne] = v;
    lg->ew[lg->ne] = w;
    lg->ne++;

    if (lg->ieN >= lg->capIE) {
        int cap = lg->capIE ? lg->capIE * 2 : 32;
        lg->ieS = (int*)mdg_realloc(lg->ieS, sizeof(int) * (size_t)cap);
        lg->ieW = (int*)mdg_realloc(lg->ieW, sizeof(int) * (size_t)cap);
        lg->ieNext = (int*)mdg_realloc(lg->ieNext, sizeof(int) * (size_t)cap);
        lg->capIE = cap;
    }
    lg->ieS[lg->ieN] = u;
    lg->ieW[lg->ieN] = w;
    lg->ieNext[lg->ieN] = lg->inHead[v];
    lg->inHead[v] = lg->ieN;
    lg->ieN++;
}

/* 复刻 dagre buildLayoutGraph 的节点插入序：
   setNode(v) 之后紧跟 setParent(v,p) 会顺带 setNode(p)，
   因此子图节点出现在它第一个成员之后。层图的 children 顺序依赖这一顺序。 */
static int* build_node_order(const MdgGraph* g, int* outN) {
    int* ord = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN > 0 ? g->nN : 1));
    int* byOrd = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN > 0 ? g->nN : 1));
    int pos = 0, i;
    for (i = 0; i < g->nN; i++) ord[i] = -1;
    for (i = 0; i < g->nN; i++) {
        int p;
        if (!g->n[i].alive) continue;
        if (ord[i] < 0) ord[i] = pos++;
        p = g->n[i].parent;
        if (p >= 0 && p < g->nN && g->n[p].alive && ord[p] < 0) ord[p] = pos++;
    }
    for (i = 0; i < g->nN; i++)
        if (ord[i] >= 0) byOrd[ord[i]] = i;
    *outN = pos;
    free(ord);
    return byOrd;
}

static void lg_build(LGraph* lg, MdgGraph* g, int rank, int relation, const int* byOrd, int nByOrd) {
    int o, e;
    lg_init(lg, g->nN, rank);
    for (o = 0; o < nByOrd; o++) {
        int i = byOrd[o];
        MdgNode* nd = &g->n[i];
        int par;
        if (!nd->alive) continue;
        if (nd->type == MDG_SUB) {
            if (!(nd->minRank <= rank && rank <= nd->maxRank)) continue;
        } else if (nd->rank != rank) {
            continue;
        }
        lg_touch(lg, i);
        par = nd->parent;
        if (par >= 0) lg_touch(lg, par);
        else par = lg->root;
        lg_set_parent(lg, i, par);
        for (e = 0; e < g->nE; e++) {
            int u;
            if (!g->e[e].alive) continue;
            if (relation == 0) {
                if (g->e[e].v != i) continue;
                u = g->e[e].u;
            } else {
                if (g->e[e].u != i) continue;
                u = g->e[e].v;
            }
            if (u < 0 || u >= g->nN) continue;
            lg_touch(lg, u);
            lg_add_edge(lg, u, i, g->e[e].weight);
        }
    }
}

typedef struct {
    int* vs;
    int nvs, cap;
    double bc;
    double weight;
    int hasBc;
} SRes;

typedef struct {
    int v;
    int* vs;
    int nvs, cap;
    int i;
    double bc;
    double weight;
    int hasBc;
    int merged;
    int indeg;
    int* in;
    int nIn, capIn;
    int* out;
    int nOut, capOut;
} Ent;

typedef struct {
    MdgGraph* g;
    LGraph* lg;
    int rank;
    int* cgu;
    int* cgv;
    int ncg, capcg;
} SortCtx;

static void grow_ints(int** arr, int* cap, int need) {
    if (need > *cap) {
        int c = *cap > 0 ? *cap : 8;
        while (c < need) c *= 2;
        *arr = (int*)mdg_realloc(*arr, sizeof(int) * (size_t)c);
        *cap = c;
    }
}

static void sres_push(SRes* r, int v) {
    grow_ints(&r->vs, &r->cap, r->nvs + 1);
    r->vs[r->nvs++] = v;
}

static void sres_free(SRes* r) {
    free(r->vs);
    memset(r, 0, sizeof(*r));
}

static void ent_push(int** arr, int* n, int* cap, int v) {
    grow_ints(arr, cap, *n + 1);
    (*arr)[(*n)++] = v;
}

static void cg_add(SortCtx* c, int u, int v) {
    int i;
    if (u < 0 || v < 0 || u == v) return;
    for (i = 0; i < c->ncg; i++)
        if (c->cgu[i] == u && c->cgv[i] == v) return;
    if (c->ncg >= c->capcg) {
        int cap = c->capcg ? c->capcg * 2 : 16;
        c->cgu = (int*)mdg_realloc(c->cgu, sizeof(int) * (size_t)cap);
        c->cgv = (int*)mdg_realloc(c->cgv, sizeof(int) * (size_t)cap);
        c->capcg = cap;
    }
    c->cgu[c->ncg] = u;
    c->cgv[c->ncg] = v;
    c->ncg++;
}

static void merge_ent(Ent* target, Ent* source) {
    double sum = 0.0, weight = 0.0;
    int i;
    if (target->weight > 0.0) {
        sum += target->bc * target->weight;
        weight += target->weight;
    }
    if (source->weight > 0.0) {
        sum += source->bc * source->weight;
        weight += source->weight;
    }
    if (source->nvs > 0) {
        int* nv;
        int n = source->nvs + target->nvs;
        nv = (int*)mdg_alloc(sizeof(int) * (size_t)n);
        for (i = 0; i < source->nvs; i++) nv[i] = source->vs[i];
        for (i = 0; i < target->nvs; i++) nv[source->nvs + i] = target->vs[i];
        free(target->vs);
        target->vs = nv;
        target->nvs = n;
        target->cap = n;
    }
    target->bc = sum / weight;
    target->hasBc = 1;
    target->weight = weight;
    if (source->i < target->i) target->i = source->i;
    source->merged = 1;
}

/* 约束合并：Forster 快速启发式 */
static void resolve_conflicts(SortCtx* c, Ent* ents, int nEnt, int* mapOf, int* out, int* nOut) {
    int* source = NULL;
    int nSource = 0, capSource = 0;
    int i, k;
    *nOut = 0;
    if (nEnt <= 0) return;
    for (i = 0; i < nEnt; i++) {
        mapOf[ents[i].v] = i;
        ents[i].merged = 0;
        ents[i].indeg = 0;
        ents[i].nIn = 0;
        ents[i].nOut = 0;
        ents[i].vs = NULL;
        ents[i].nvs = 0;
        ents[i].cap = 0;
        ent_push(&ents[i].vs, &ents[i].nvs, &ents[i].cap, ents[i].v);
    }
    for (k = 0; k < c->ncg; k++) {
        int a = mapOf[c->cgu[k]];
        int b = mapOf[c->cgv[k]];
        if (a < 0 || b < 0) continue;
        ents[b].indeg++;
        ent_push(&ents[a].out, &ents[a].nOut, &ents[a].capOut, b);
    }
    for (i = 0; i < nEnt; i++)
        if (ents[i].indeg == 0) ent_push(&source, &nSource, &capSource, i);

    while (nSource > 0) {
        int e = source[--nSource];
        int x;
        out[(*nOut)++] = e;
        for (x = ents[e].nIn - 1; x >= 0; x--) {
            Ent* u = &ents[ents[e].in[x]];
            if (u->merged) continue;
            if (!u->hasBc || !ents[e].hasBc || u->bc >= ents[e].bc) merge_ent(&ents[e], u);
        }
        for (x = 0; x < ents[e].nOut; x++) {
            Ent* w = &ents[ents[e].out[x]];
            ent_push(&w->in, &w->nIn, &w->capIn, e);
            if (--w->indeg == 0) ent_push(&source, &nSource, &capSource, ents[e].out[x]);
        }
    }
    free(source);

    {
        int m = 0;
        for (i = 0; i < *nOut; i++) {
            int e = out[i];
            if (ents[e].merged) continue;
            out[m++] = e;
        }
        *nOut = m;
    }
}

static int cmp_ent_bias(const Ent* a, const Ent* b, int biasRight) {
    if (a->bc < b->bc) return -1;
    if (a->bc > b->bc) return 1;
    return biasRight ? (b->i - a->i) : (a->i - b->i);
}

static void sort_sub(SortCtx* c, int v, int biasRight, SRes* out) {
    MdgGraph* g = c->g;
    LGraph* lg = c->lg;
    int* kids = NULL;
    int nk = 0, capk = 0, k, bl = -1, br = -1;
    Ent* ents;
    SRes* subs;
    SRes** subOf;
    int* mapOf;
    int* orderIdx = NULL;
    int nOrd = 0, capOrd = 0;
    int* sortables = NULL;
    int nSort = 0, capSort = 0;
    int* unsortables = NULL;
    int nUnsort = 0, capUnsort = 0;
    int i;
    double sum = 0.0, tw = 0.0;

    memset(out, 0, sizeof(*out));

    for (k = lg->chead[v]; k >= 0; k = lg->cnext[k]) ent_push(&kids, &nk, &capk, k);

    if (v >= 0 && v < g->nN && g->n[v].type == MDG_SUB && g->n[v].bl != NULL) {
        int slot = c->rank - g->n[v].minRank;
        if (slot >= 0 && slot <= g->n[v].maxRank - g->n[v].minRank) {
            bl = g->n[v].bl[slot];
            br = g->n[v].br[slot];
        }
    }
    if (bl >= 0) {
        int m = 0;
        for (k = 0; k < nk; k++)
            if (kids[k] != bl && kids[k] != br) kids[m++] = kids[k];
        nk = m;
    }

    if (nk <= 0) {
        if (bl >= 0) {
            sres_push(out, bl);
            sres_push(out, br);
        }
        free(kids);
        return;
    }

    ents = (Ent*)mdg_alloc(sizeof(Ent) * (size_t)nk);
    subs = (SRes*)mdg_alloc(sizeof(SRes) * (size_t)nk);
    subOf = (SRes**)mdg_alloc(sizeof(SRes*) * (size_t)(g->nN + 1));
    mapOf = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN + 1));
    for (i = 0; i < g->nN + 1; i++) mapOf[i] = -1;

    for (k = 0; k < nk; k++) {
        int w = kids[k];
        Ent* e = &ents[k];
        double s = 0.0, wt = 0.0;
        int cnt = 0, x;
        e->v = w;
        e->i = k;
        e->hasBc = 0;
        e->bc = 0.0;
        e->weight = 0.0;
        e->merged = 0;
        for (x = lg->inHead[w]; x >= 0; x = lg->ieNext[x]) {
            int u = lg->ieS[x];
            s += (double)lg->ieW[x] * (double)g->n[u].order;
            wt += (double)lg->ieW[x];
            cnt++;
        }
        if (cnt > 0) {
            e->hasBc = 1;
            e->bc = s / wt;
            e->weight = wt;
        }
    }


    for (k = 0; k < nk; k++) {
        if (lg->chead[kids[k]] < 0) continue;
        sort_sub(c, kids[k], biasRight, &subs[k]);
        subOf[kids[k]] = &subs[k];
        if (subs[k].hasBc) {
            if (ents[k].hasBc) {
                double tw2 = ents[k].weight + subs[k].weight;
                ents[k].bc = (ents[k].bc * ents[k].weight + subs[k].bc * subs[k].weight) / tw2;
                ents[k].weight = tw2;
            } else {
                ents[k].bc = subs[k].bc;
                ents[k].weight = subs[k].weight;
                ents[k].hasBc = 1;
            }
        }
    }

    {
        int* buf = (int*)mdg_alloc(sizeof(int) * (size_t)nk);
        int nb = 0;
        resolve_conflicts(c, ents, nk, mapOf, buf, &nb);
        for (k = 0; k < nb; k++) ent_push(&orderIdx, &nOrd, &capOrd, buf[k]);
        free(buf);
    }

    /* expandSubgraphs */
    for (k = 0; k < nOrd; k++) {
        Ent* e = &ents[orderIdx[k]];
        int* nv = NULL;
        int nn = 0, capn = 0, q;
        for (q = 0; q < e->nvs; q++) {
            int p = e->vs[q];
            if (p >= 0 && p <= g->nN && subOf[p] != NULL) {
                int t;
                for (t = 0; t < subOf[p]->nvs; t++) ent_push(&nv, &nn, &capn, subOf[p]->vs[t]);
            } else {
                ent_push(&nv, &nn, &capn, p);
            }
        }
        free(e->vs);
        e->vs = nv;
        e->nvs = nn;
        e->cap = capn;
    }

    /* sort：有重心的按重心升序，无重心的按 i 降序从尾部消费 */
    for (k = 0; k < nOrd; k++) {
        Ent* e = &ents[orderIdx[k]];
        if (e->hasBc) ent_push(&sortables, &nSort, &capSort, orderIdx[k]);
        else ent_push(&unsortables, &nUnsort, &capUnsort, orderIdx[k]);
    }
    for (i = 1; i < nSort; i++) {
        int cur = sortables[i], j = i - 1;
        while (j >= 0 && cmp_ent_bias(&ents[sortables[j]], &ents[cur], biasRight) > 0) {
            sortables[j + 1] = sortables[j];
            j--;
        }
        sortables[j + 1] = cur;
    }
    for (i = 1; i < nUnsort; i++) {
        int cur = unsortables[i], j = i - 1;
        while (j >= 0 && ents[unsortables[j]].i < ents[cur].i) {
            unsortables[j + 1] = unsortables[j];
            j--;
        }
        unsortables[j + 1] = cur;
    }

    {
        int vsIndex = 0, ui = 0;
        while (nUnsort - ui > 0) {
            int last = unsortables[nUnsort - 1 - ui];
            Ent* e = &ents[last];
            if (e->i > vsIndex) break;
            ui++;
            for (k = 0; k < e->nvs; k++) sres_push(out, e->vs[k]);
            vsIndex++;
        }
        for (i = 0; i < nSort; i++) {
            Ent* e = &ents[sortables[i]];
            vsIndex += e->nvs;
            for (k = 0; k < e->nvs; k++) sres_push(out, e->vs[k]);
            sum += e->bc * e->weight;
            tw += e->weight;
            while (nUnsort - ui > 0) {
                int last = unsortables[nUnsort - 1 - ui];
                Ent* u = &ents[last];
                if (u->i > vsIndex) break;
                ui++;
                for (k = 0; k < u->nvs; k++) sres_push(out, u->vs[k]);
                vsIndex++;
            }
        }
        if (tw > 0.0) {
            out->bc = sum / tw;
            out->weight = tw;
            out->hasBc = 1;
        }
    }

    if (bl >= 0) {
        int n0 = out->nvs;
        int* nv = (int*)mdg_alloc(sizeof(int) * (size_t)(n0 + 2));
        int predL = -1, predR = -1, x;
        nv[0] = bl;
        for (k = 0; k < n0; k++) nv[k + 1] = out->vs[k];
        nv[n0 + 1] = br;
        free(out->vs);
        out->vs = nv;
        out->nvs = n0 + 2;
        out->cap = n0 + 2;
        for (x = lg->inHead[bl]; x >= 0; x = lg->ieNext[x]) {
            predL = lg->ieS[x];
            break;
        }
        for (x = lg->inHead[br]; x >= 0; x = lg->ieNext[x]) {
            predR = lg->ieS[x];
            break;
        }
        if (predL >= 0) {
            double blPred = (double)g->n[predL].order;
            double brPred = (predR >= 0) ? (double)g->n[predR].order : 0.0;
            double w2;
            if (!out->hasBc) {
                out->bc = 0.0;
                out->weight = 0.0;
                out->hasBc = 1;
            }
            w2 = out->weight + 2.0;
            out->bc = (out->bc * out->weight + blPred + brPred) / w2;
            out->weight = w2;
        }
    }

    for (k = 0; k < nk; k++) sres_free(&subs[k]);
    for (k = 0; k < nk; k++) {
        free(ents[k].vs);
        free(ents[k].in);
        free(ents[k].out);
    }
    free(ents);
    free(subs);
    free(subOf);
    free(mapOf);
    free(kids);
    free(orderIdx);
    free(sortables);
    free(unsortables);
}

static void add_subgraph_constraints(SortCtx* c, const int* vs, int nvs) {
    LGraph* lg = c->lg;
    int* prev = (int*)mdg_alloc(sizeof(int) * (size_t)lg->n);
    int rootPrev = -1;
    int i;
    for (i = 0; i < lg->n; i++) prev[i] = -1;
    for (i = 0; i < nvs; i++) {
        int child = lg->parent[vs[i]];
        while (child >= 0) {
            int par = (child == lg->root) ? -1 : lg->parent[child];
            int prevChild;
            if (par >= 0) {
                prevChild = prev[par];
                prev[par] = child;
            } else {
                prevChild = rootPrev;
                rootPrev = child;
            }
            if (prevChild >= 0 && prevChild != child) {
                cg_add(c, prevChild, child);
                break;
            }
            child = par;
        }
    }
    free(prev);
}

static void sweep_one(MdgGraph* g, SortCtx* c, LGraph* lg, int biasRight) {
    SRes r;
    int i;
    memset(&r, 0, sizeof(r));
    c->lg = lg;
    c->rank = lg->rank;
    sort_sub(c, lg->root, biasRight, &r);
    for (i = 0; i < r.nvs; i++) {
        int v = r.vs[i];
        if (v >= 0 && v < g->nN && g->n[v].type != MDG_SUB) g->n[v].order = i;
    }
    add_subgraph_constraints(c, r.vs, r.nvs);
    sres_free(&r);
}

/* 交叉计数：Barth 双层计数法（带权） */
static int cross_count(const MdgGraph* g, const Layering* L) {
    int cc = 0, r;
    int* posBuf = NULL;
    int* wtBuf = NULL;
    int capBuf = 0;
    if (L->nRanks <= 1) return 0;
    for (r = 1; r < L->nRanks; r++) {
        int nStart = L->start[r - 1], nEnd = L->start[r];
        int sLen = L->start[r + 1] - L->start[r];
        int firstIndex = 1, treeSize, i, n;
        int* tree;
        if (sLen <= 0) continue;
        while (firstIndex < sLen) firstIndex <<= 1;
        treeSize = 2 * firstIndex - 1;
        firstIndex -= 1;
        tree = (int*)mdg_alloc(sizeof(int) * (size_t)treeSize);
        n = 0;
        for (i = nStart; i < nEnd; i++) {
            int v = L->nodes[i], e, j, seg = n;
            for (e = 0; e < g->nE; e++) {
                int w;
                if (!g->e[e].alive) continue;
                if (g->e[e].u != v) continue;
                w = g->e[e].v;
                if (g->n[w].rank != r) continue;
                if (n + 1 > capBuf) {
                    int cap = capBuf ? capBuf * 2 : 64;
                    while (cap < n + 1) cap *= 2;
                    posBuf = (int*)mdg_realloc(posBuf, sizeof(int) * (size_t)cap);
                    wtBuf = (int*)mdg_realloc(wtBuf, sizeof(int) * (size_t)cap);
                    capBuf = cap;
                }
                posBuf[n] = g->n[w].order;
                wtBuf[n] = g->e[e].weight;
                n++;
            }
            /* 只对本北层节点自己的那一段按 pos 升序插入排序（dagre: _.sortBy(list,'pos')） */
            for (j = seg + 1; j < n; j++) {
                int curP = posBuf[j], curW = wtBuf[j], k = j - 1;
                if (posBuf[k] <= curP) continue;
                while (k >= seg && posBuf[k] > curP) {
                    posBuf[k + 1] = posBuf[k];
                    wtBuf[k + 1] = wtBuf[k];
                    k--;
                }
                posBuf[k + 1] = curP;
                wtBuf[k + 1] = curW;
            }
        }
        for (i = 0; i < n; i++) {
            int index = posBuf[i] + firstIndex;
            int weightSum = 0;
            tree[index] += wtBuf[i];
            while (index > 0) {
                if (index & 1) weightSum += tree[index + 1];
                index = (index - 1) >> 1;
                tree[index] += wtBuf[i];
            }
            cc += wtBuf[i] * weightSum;
        }
        free(tree);
    }
    free(posBuf);
    free(wtBuf);
    return cc;
}

/* initOrder：按 rank 升序 DFS 给出初始层内顺序 */
static void init_order_dfs(const MdgGraph* g, Layering* L, int v, unsigned char* vis, int* fill) {    int e;
    if (vis[v]) return;
    vis[v] = 1;
    {
        int r = g->n[v].rank;
        int slot = L->start[r] + fill[r]++;
        L->nodes[slot] = v;
        g->n[v].order = slot - L->start[r];
    }
    for (e = 0; e < g->nE; e++) {
        if (!g->e[e].alive) continue;
        if (g->e[e].u != v) continue;
        init_order_dfs(g, L, g->e[e].v, vis, fill);
    }
}

static void init_order(const MdgGraph* g, Layering* L, int* seed, int nSeed) {
    unsigned char* vis = (unsigned char*)mdg_alloc((size_t)(g->nN > 0 ? g->nN : 1));
    int* fill = (int*)mdg_alloc(sizeof(int) * (size_t)(L->nRanks + 1));
    int i;
    for (i = 0; i < nSeed; i++) init_order_dfs(g, L, seed[i], vis, fill);
    free(vis);
    free(fill);
}

static void mdg_order(MdgGraph* g) {
    int maxR = -1, i, r, nIter;
    LGraph* down = NULL;
    LGraph* up = NULL;
    int nDown = 0, nUp = 0;
    Layering L;
    Layering best;
    Layering cur;
    int* seed = NULL;
    int nSeed = 0, capSeed = 0;
    int bestCC, lastBest;

    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        if (g->n[i].rank > maxR) maxR = g->n[i].rank;
    }
    if (maxR < 0) return;

    nDown = maxR;
    nUp = maxR;
    if (nDown > 0) down = (LGraph*)mdg_alloc(sizeof(LGraph) * (size_t)nDown);
    if (nUp > 0) up = (LGraph*)mdg_alloc(sizeof(LGraph) * (size_t)nUp);
    {
        int nByOrd = 0;
        int* byOrd = build_node_order(g, &nByOrd);
        for (r = 1; r <= maxR; r++) lg_build(&down[r - 1], g, r, 0, byOrd, nByOrd);
        for (r = maxR - 1; r >= 0; r--) lg_build(&up[maxR - 1 - r], g, r, 1, byOrd, nByOrd);

        /* 初始顺序：seed 按 rank 升序稳定排序（dagre: sortBy(simpleNodes, rank)） */
        {
            int* arr = (int*)mdg_alloc(sizeof(int) * (size_t)(nByOrd > 0 ? nByOrd : 1));
            int m = 0, a, j;
            for (a = 0; a < nByOrd; a++) {
                int v = byOrd[a];
                if (!g->n[v].alive || g->n[v].type == MDG_SUB) continue;
                arr[m++] = v;
            }
            for (a = 1; a < m; a++) {
                int curI = arr[a], rk = g->n[curI].rank;
                j = a - 1;
                while (j >= 0 && g->n[arr[j]].rank > rk) {
                    arr[j + 1] = arr[j];
                    j--;
                }
                arr[j + 1] = curI;
            }
            for (a = 0; a < m; a++) ent_push(&seed, &nSeed, &capSeed, arr[a]);
            free(arr);
        }
        free(byOrd);
    }

    memset(&L, 0, sizeof(L));
    L.nRanks = maxR + 1;
    L.n = nSeed;
    L.start = (int*)mdg_alloc(sizeof(int) * (size_t)(maxR + 2));
    L.nodes = (int*)mdg_alloc(sizeof(int) * (size_t)(nSeed > 0 ? nSeed : 1));
    L.ord = (int*)mdg_alloc(sizeof(int) * (size_t)(g->nN > 0 ? g->nN : 1));
    for (i = 0; i < nSeed; i++) L.start[g->n[seed[i]].rank + 1]++;
    for (r = 0; r < L.nRanks; r++) L.start[r + 1] += L.start[r];
    init_order(g, &L, seed, nSeed);

    layering_from_order(&best, g);
    /* dagre：bestCC 初值为 +Inf，初始 layering 仅作为起跳点，不参与竞争 */
    bestCC = MDG_INF;
    lastBest = 0;

    memset(&cur, 0, sizeof(cur));
    for (nIter = 0; lastBest < 4 && nIter < 200; nIter++, lastBest++) {
        int bias = ((nIter % 4) >= 2);
        SortCtx c;
        memset(&c, 0, sizeof(c));
        c.g = g;
        if (nIter % 2) {
            for (i = 0; i < nDown; i++) sweep_one(g, &c, &down[i], bias);
        } else {
            for (i = 0; i < nUp; i++) sweep_one(g, &c, &up[i], bias);
        }
        free(c.cgu);
        free(c.cgv);
        layering_from_order(&cur, g);
        {
            int cc = cross_count(g, &cur);
            if (cc < bestCC) {
                lastBest = 0;
                bestCC = cc;
                layering_free(&best);
                best = cur;
                memset(&cur, 0, sizeof(cur));
            } else {
                layering_free(&cur);
                memset(&cur, 0, sizeof(cur));
            }
        }
    }
    layering_free(&cur);

    for (r = 0; r < best.nRanks; r++) {
        int k;
        for (k = best.start[r]; k < best.start[r + 1]; k++) {
            int v = best.nodes[k];
            g->n[v].order = k - best.start[r];
        }
    }
    layering_free(&best);
    layering_free(&L);

    for (i = 0; i < nDown; i++) lg_free(&down[i]);
    for (i = 0; i < nUp; i++) lg_free(&up[i]);
    free(down);
    free(up);
    free(seed);
}

/* ---------- 9. position ---------- */

static void position_y(MdgGraph* g, const Layering* L) {
    int r, prevY = 0;
    for (r = 0; r < L->nRanks; r++) {
        int s = L->start[r], e = L->start[r + 1];
        int maxH = 0, i;
        for (i = s; i < e; i++)
            if (g->n[L->nodes[i]].h > maxH) maxH = g->n[L->nodes[i]].h;
        for (i = s; i < e; i++) g->n[L->nodes[i]].y = prevY + maxH / 2;
        prevY += maxH + g->ranksep;
    }
}

/* 冲突矩阵（对称，存上三角） */
static unsigned char* conf_new(int n) {
    return (unsigned char*)mdg_alloc((size_t)n * (size_t)n);
}

static int conf_has(const unsigned char* c, int n, int v, int w) {
    if (v > w) {
        int t = v;
        v = w;
        w = t;
    }
    return c[(size_t)v * (size_t)n + (size_t)w];
}

static void conf_add(unsigned char* c, int n, int v, int w) {
    if (v == w) return;
    if (v > w) {
        int t = v;
        v = w;
        w = t;
    }
    c[(size_t)v * (size_t)n + (size_t)w] = 1;
}

/* type1 冲突：非内段跨过内段 */
static void find_type1_conflicts(unsigned char* conf, const MdgGraph* g, const Layering* L) {
    int r;
    for (r = 1; r < L->nRanks; r++) {
        int prevLen = L->start[r] - L->start[r - 1];
        int ca = L->start[r], cb = L->start[r + 1];
        int cnt = cb - ca;
        int k0 = 0, scanPos = 0, i, e;
        if (cnt <= 0) continue;
        for (i = 0; i < cnt; i++) {
            int v = L->nodes[ca + i];
            int w = -1, k1, k;
            if (is_dummy(g, v)) {
                for (e = 0; e < g->nE; e++) {
                    if (!g->e[e].alive || g->e[e].v != v) continue;
                    if (is_dummy(g, g->e[e].u)) {
                        w = g->e[e].u;
                        break;
                    }
                }
            }
            k1 = (w >= 0) ? g->n[w].order : prevLen;
            if (w >= 0 || i == cnt - 1) {
                for (k = scanPos; k <= i; k++) {
                    int sn = L->nodes[ca + k];
                    for (e = 0; e < g->nE; e++) {
                        int u, uPos;
                        if (!g->e[e].alive || g->e[e].v != sn) continue;
                        u = g->e[e].u;
                        if (g->n[u].rank != r - 1) continue;
                        uPos = g->n[u].order;
                        if ((uPos < k0 || k1 < uPos) && !(is_dummy(g, u) && is_dummy(g, sn)))
                            conf_add(conf, g->nN, u, sn);
                    }
                }
                scanPos = i + 1;
                k0 = k1;
            }
        }
    }
}

/* type2 冲突：子图边框上下跨层边界时，两侧 dummy 不允许对齐 */
static void scan_type2(unsigned char* conf, const MdgGraph* g, const Layering* L, int rank,
                       int from, int to, int prevN, int nextN) {
    int i, e;
    int cnt = L->start[rank + 1] - L->start[rank];
    if (to > cnt) to = cnt;
    if (from < 0) from = 0;
    for (i = from; i < to; i++) {
        int v = L->nodes[L->start[rank] + i];
        if (!is_dummy(g, v)) continue;
        for (e = 0; e < g->nE; e++) {
            int u;
            if (!g->e[e].alive || g->e[e].v != v) continue;
            u = g->e[e].u;
            if (g->n[u].rank != rank - 1) continue;
            if (!is_dummy(g, u)) continue;
            if (g->n[u].order < prevN || g->n[u].order > nextN) conf_add(conf, g->nN, u, v);
        }
    }
}

static void find_type2_conflicts(unsigned char* conf, const MdgGraph* g, const Layering* L) {
    int r;
    for (r = 1; r < L->nRanks; r++) {
        int northLen = L->start[r] - L->start[r - 1];
        int southLen = L->start[r + 1] - L->start[r];
        /* dagre：prevNorthPos 初值 -1，nextNorthPos 初值 undefined
           （undefined 参与比较恒为 false，因此未遇到边框时不做越界判定） */
        int prevN = -1, nextN = 0, hasNext = 0;
        int southPos = 0, look, e;
        if (southLen <= 0) continue;
        for (look = 0; look < southLen; look++) {
            int v = L->nodes[L->start[r] + look];
            if (g->n[v].type == MDG_BORDER) {
                int firstPred = -1;
                for (e = 0; e < g->nE; e++) {
                    if (!g->e[e].alive || g->e[e].v != v) continue;
                    if (g->n[g->e[e].u].rank != r - 1) continue;
                    firstPred = g->e[e].u;
                    break;
                }
                if (firstPred >= 0) {
                    nextN = g->n[firstPred].order;
                    hasNext = 1;
                    scan_type2(conf, g, L, r, southPos, look, prevN, nextN);
                    southPos = look;
                    prevN = nextN;
                }
            }
            scan_type2(conf, g, L, r, southPos, southLen, hasNext ? nextN : -1, northLen);
        }
    }
}

/* 层矩阵变换：up=1 保持层序，up=0 层序倒置；rev=1 层内倒序 */
static void layering_transform(Layering* out, const Layering* in, int up, int rev, int nN) {
    int r, i;
    int nb = in->nRanks;
    memset(out, 0, sizeof(*out));
    out->nRanks = nb;
    out->n = in->n;
    out->nodes = (int*)mdg_alloc(sizeof(int) * (size_t)(in->n > 0 ? in->n : 1));
    out->start = (int*)mdg_alloc(sizeof(int) * (size_t)(nb + 1));
    out->ord = (int*)mdg_alloc(sizeof(int) * (size_t)(nN > 0 ? nN : 1));
    for (r = 0; r < nb; r++) {
        int tr = up ? r : (nb - 1 - r);
        out->start[tr + 1] = in->start[r + 1] - in->start[r];
    }
    for (r = 0; r < nb; r++) out->start[r + 1] += out->start[r];
    {
        int* cur = (int*)mdg_alloc(sizeof(int) * (size_t)(nb + 1));
        memcpy(cur, out->start, sizeof(int) * (size_t)(nb + 1));
        for (r = 0; r < nb; r++) {
            int tr = up ? r : (nb - 1 - r);
            int s = in->start[r], cnt = in->start[r + 1] - s;
            for (i = 0; i < cnt; i++) {
                int idx = rev ? (s + cnt - 1 - i) : (s + i);
                int v = in->nodes[idx];
                int slot = cur[tr]++;
                out->nodes[slot] = v;
                out->ord[v] = slot - out->start[tr];
            }
        }
        free(cur);
    }
}

/* BK 垂直对齐：把节点串成块 */
static void vertical_alignment(int* root, int* align, int* pos, const MdgGraph* g,
                               const Layering* L, const unsigned char* conf, int up) {
    int r, i, e;
    int nN = g->nN;
    for (i = 0; i < nN; i++) {
        root[i] = i;
        align[i] = i;
        pos[i] = 0;
    }
    for (r = 0; r < L->nRanks; r++) {
        int s = L->start[r], e2 = L->start[r + 1];
        for (i = s; i < e2; i++) pos[L->nodes[i]] = i - s;
    }
    for (r = 0; r < L->nRanks; r++) {
        int s = L->start[r], e2 = L->start[r + 1];
        int prevIdx = -1;
        for (i = s; i < e2; i++) {
            int v = L->nodes[i];
            int nb[192], nn = 0, k, j;
            for (e = 0; e < g->nE && nn < 192; e++) {
                int other;
                if (!g->e[e].alive) continue;
                if (up) {
                    if (g->e[e].v != v) continue;
                    other = g->e[e].u;
                } else {
                    if (g->e[e].u != v) continue;
                    other = g->e[e].v;
                }
                nb[nn++] = other;
            }
            if (nn <= 0) continue;
            for (k = 1; k < nn; k++) {
                int cur = nb[k];
                j = k - 1;
                while (j >= 0 && pos[nb[j]] > pos[cur]) {
                    nb[j + 1] = nb[j];
                    j--;
                }
                nb[j + 1] = cur;
            }
            {
                double mp = (nn - 1) / 2.0;
                int lo = (int)mp;
                int hi = (int)(mp + 0.999999);
                for (k = lo; k <= hi && k < nn; k++) {
                    int w = nb[k];
                    if (align[v] == v && prevIdx < pos[w] && !conf_has(conf, nN, v, w)) {
                        align[w] = v;
                        root[v] = root[w];
                        align[v] = root[v];
                        prevIdx = pos[w];
                    }
                }
            }
        }
    }
}

static double bk_sep(const MdgGraph* g, int v, int w) {
    double sum = 0.0;
    sum += (double)g->n[v].w / 2.0;
    sum += (double)(is_dummy(g, v) ? g->edgesep : g->nodesep) / 2.0;
    sum += (double)(is_dummy(g, w) ? g->edgesep : g->nodesep) / 2.0;
    sum += (double)g->n[w].w / 2.0;
    return sum;
}

/* 块图 */
typedef struct {
    int* nodes;
    int nNodes, capNodes;
    int* eu;
    int* ev;
    double* ew;
    int ne, capE;
    int* outOff;
    int* outTo;
    double* outW;
    int* inOff;
    int* inTo;
    double* inW;
} BlockG;

static void bg_init(BlockG* b) {
    memset(b, 0, sizeof(*b));
}

static void bg_free(BlockG* b) {
    free(b->nodes);
    free(b->eu);
    free(b->ev);
    free(b->ew);
    free(b->outOff);
    free(b->outTo);
    free(b->outW);
    free(b->inOff);
    free(b->inTo);
    free(b->inW);
    memset(b, 0, sizeof(*b));
}

static void bg_node(BlockG* b, int v) {
    int i;
    for (i = 0; i < b->nNodes; i++)
        if (b->nodes[i] == v) return;
    if (b->nNodes >= b->capNodes) {
        int cap = b->capNodes ? b->capNodes * 2 : 16;
        b->nodes = (int*)mdg_realloc(b->nodes, sizeof(int) * (size_t)cap);
        b->capNodes = cap;
    }
    b->nodes[b->nNodes++] = v;
}

static void bg_edge(BlockG* b, int u, int v, double w) {
    int i;
    for (i = 0; i < b->ne; i++) {
        if (b->eu[i] == u && b->ev[i] == v) {
            if (w > b->ew[i]) b->ew[i] = w;
            return;
        }
    }
    if (b->ne >= b->capE) {
        int cap = b->capE ? b->capE * 2 : 32;
        b->eu = (int*)mdg_realloc(b->eu, sizeof(int) * (size_t)cap);
        b->ev = (int*)mdg_realloc(b->ev, sizeof(int) * (size_t)cap);
        b->ew = (double*)mdg_realloc(b->ew, sizeof(double) * (size_t)cap);
        b->capE = cap;
    }
    b->eu[b->ne] = u;
    b->ev[b->ne] = v;
    b->ew[b->ne] = w;
    b->ne++;
}

static void bg_index(BlockG* b, int nN) {
    int i;
    int* slot = (int*)mdg_alloc(sizeof(int) * (size_t)nN);
    for (i = 0; i < nN; i++) slot[i] = -1;
    for (i = 0; i < b->nNodes; i++) slot[b->nodes[i]] = i;
    b->outOff = (int*)mdg_alloc(sizeof(int) * (size_t)(b->nNodes + 1));
    b->inOff = (int*)mdg_alloc(sizeof(int) * (size_t)(b->nNodes + 1));
    b->outTo = (int*)mdg_alloc(sizeof(int) * (size_t)(b->ne > 0 ? b->ne : 1));
    b->outW = (double*)mdg_alloc(sizeof(double) * (size_t)(b->ne > 0 ? b->ne : 1));
    b->inTo = (int*)mdg_alloc(sizeof(int) * (size_t)(b->ne > 0 ? b->ne : 1));
    b->inW = (double*)mdg_alloc(sizeof(double) * (size_t)(b->ne > 0 ? b->ne : 1));
    for (i = 0; i < b->nNodes; i++) {
        b->outOff[i + 1] = 0;
        b->inOff[i + 1] = 0;
    }
    for (i = 0; i < b->ne; i++) {
        b->outOff[slot[b->eu[i]] + 1]++;
        b->inOff[slot[b->ev[i]] + 1]++;
    }
    for (i = 0; i < b->nNodes; i++) {
        b->outOff[i + 1] += b->outOff[i];
        b->inOff[i + 1] += b->inOff[i];
    }
    {
        int* oc = (int*)mdg_alloc(sizeof(int) * (size_t)(b->nNodes + 1));
        int* ic = (int*)mdg_alloc(sizeof(int) * (size_t)(b->nNodes + 1));
        memcpy(oc, b->outOff, sizeof(int) * (size_t)(b->nNodes + 1));
        memcpy(ic, b->inOff, sizeof(int) * (size_t)(b->nNodes + 1));
        for (i = 0; i < b->ne; i++) {
            int a = slot[b->eu[i]], c = slot[b->ev[i]];
            int p = oc[a]++;
            int q = ic[c]++;
            b->outTo[p] = c;
            b->outW[p] = b->ew[i];
            b->inTo[q] = a;
            b->inW[q] = b->ew[i];
        }
        free(oc);
        free(ic);
    }
    free(slot);
}

typedef struct {
    int* buf;
    int n, cap;
} IntStack;

static void st_push(IntStack* s, int v) {
    if (s->n >= s->cap) {
        int cap = s->cap ? s->cap * 2 : 32;
        s->buf = (int*)mdg_realloc(s->buf, sizeof(int) * (size_t)cap);
        s->cap = cap;
    }
    s->buf[s->n++] = v;
}

/* 块图上的两遍遍历，语义对齐 dagre 的 iterate() */
static void block_iterate(BlockG* bg, double* xs, const MdgGraph* g, int reverseSep, int pass2) {
    IntStack st;
    unsigned char* vis = (unsigned char*)mdg_alloc((size_t)(bg->nNodes > 0 ? bg->nNodes : 1));
    int borderType = reverseSep ? 1 : 2;
    int i;
    memset(&st, 0, sizeof(st));
    for (i = 0; i < bg->nNodes; i++) st_push(&st, i);
    while (st.n > 0) {
        int elem = st.buf[--st.n];
        if (vis[elem]) {
            int nodeId = bg->nodes[elem];
            if (!pass2) {
                double best = 0.0;
                int k;
                for (k = bg->inOff[elem]; k < bg->inOff[elem + 1]; k++) {
                    int u = bg->inTo[k];
                    double cand = xs[bg->nodes[u]] + bg->inW[k];
                    if (cand > best) best = cand;
                }
                xs[nodeId] = best;
            } else {
                double minv = 1e300;
                int k;
                for (k = bg->outOff[elem]; k < bg->outOff[elem + 1]; k++) {
                    int w = bg->outTo[k];
                    double cand = xs[bg->nodes[w]] - bg->outW[k];
                    if (cand < minv) minv = cand;
                }
                if (minv < 1e299 && g->n[nodeId].borderType != borderType && minv > xs[nodeId])
                    xs[nodeId] = minv;
            }
        } else {
            int k;
            vis[elem] = 1;
            st_push(&st, elem);
            if (!pass2) {
                for (k = bg->inOff[elem]; k < bg->inOff[elem + 1]; k++) st_push(&st, bg->inTo[k]);
            } else {
                for (k = bg->outOff[elem]; k < bg->outOff[elem + 1]; k++) st_push(&st, bg->outTo[k]);
            }
        }
    }
    free(vis);
    free(st.buf);
}

static void horizontal_compaction(double* xs, const MdgGraph* g, const Layering* L,
                                  const int* root, int reverseSep) {
    BlockG bg;
    int r, i;
    int nN = g->nN;
    bg_init(&bg);
    for (r = 0; r < L->nRanks; r++) {
        int s = L->start[r], e = L->start[r + 1];
        int u = -1;
        for (i = s; i < e; i++) {
            int v = L->nodes[i];
            int vr = root[v];
            bg_node(&bg, vr);
            if (u >= 0) {
                int ur = root[u];
                bg_edge(&bg, ur, vr, bk_sep(g, v, u));
            }
            u = v;
        }
    }
    for (i = 0; i < nN; i++) xs[i] = 0.0;
    if (bg.nNodes <= 0) {
        bg_free(&bg);
        return;
    }
    bg_index(&bg, nN);
    block_iterate(&bg, xs, g, reverseSep, 0);
    block_iterate(&bg, xs, g, reverseSep, 1);
    for (i = 0; i < nN; i++) xs[i] = xs[root[i]];
    bg_free(&bg);
}

static double align_width(const MdgGraph* g, const double* xs) {
    double lo = 1e300, hi = -1e300;
    int i;
    for (i = 0; i < g->nN; i++) {
        double a, b;
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        a = xs[i] - (double)g->n[i].w / 2.0;
        b = xs[i] + (double)g->n[i].w / 2.0;
        if (a < lo) lo = a;
        if (b > hi) hi = b;
    }
    if (lo > hi) return 0.0;
    return hi - lo;
}

static void mdg_position_x(MdgGraph* g, const Layering* L) {
    int nN = g->nN;
    unsigned char* conf = conf_new(nN);
    double* xs[4];
    double* snap[4];
    int* root = (int*)mdg_alloc(sizeof(int) * (size_t)nN);
    int* align = (int*)mdg_alloc(sizeof(int) * (size_t)nN);
    int* pos = (int*)mdg_alloc(sizeof(int) * (size_t)nN);
    Layering Mt;
    int s, i, best;
    double aMin = 1e300, aMax = -1e300;

    find_type1_conflicts(conf, g, L);
    find_type2_conflicts(conf, g, L);

    for (s = 0; s < 4; s++) {
        int up = (s < 2);
        int rev = (s & 1);
        xs[s] = (double*)mdg_alloc(sizeof(double) * (size_t)nN);
        layering_transform(&Mt, L, up, rev, nN);
        vertical_alignment(root, align, pos, g, &Mt, conf, up);
        horizontal_compaction(xs[s], g, &Mt, root, rev);
        if (rev) {
            for (i = 0; i < nN; i++) xs[s][i] = -xs[s][i];
        }
        layering_free(&Mt);
    }

    best = 0;
    for (s = 0; s < 4; s++) {
        snap[s] = (double*)mdg_alloc(sizeof(double) * (size_t)nN);
        memcpy(snap[s], xs[s], sizeof(double) * (size_t)nN);
    }
    {
        double bw = align_width(g, xs[0]);
        for (s = 1; s < 4; s++) {
            double w = align_width(g, xs[s]);
            if (w < bw) {
                bw = w;
                best = s;
            }
        }
    }
    for (i = 0; i < nN; i++) {
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        if (xs[best][i] < aMin) aMin = xs[best][i];
        if (xs[best][i] > aMax) aMax = xs[best][i];
    }
    for (s = 0; s < 4; s++) {
        double delta;
        if (s == best) continue;
        if (s & 1) {
            double hi = -1e300;
            for (i = 0; i < nN; i++) {
                if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
                if (xs[s][i] > hi) hi = xs[s][i];
            }
            delta = aMax - hi;
        } else {
            double lo = 1e300;
            for (i = 0; i < nN; i++) {
                if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
                if (xs[s][i] < lo) lo = xs[s][i];
            }
            delta = aMin - lo;
        }
        if (delta != 0.0) {
            for (i = 0; i < nN; i++) {
                if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
                xs[s][i] += delta;
            }
        }
    }

    for (i = 0; i < nN; i++) {
        double v4[4];
        int a, j;
        if (!g->n[i].alive || g->n[i].type == MDG_SUB) continue;
        for (a = 0; a < 4; a++) v4[a] = xs[a][i];
        for (a = 1; a < 4; a++) {
            double cur = v4[a];
            j = a - 1;
            while (j >= 0 && v4[j] > cur) {
                v4[j + 1] = v4[j];
                j--;
            }
            v4[j + 1] = cur;
        }
        g->n[i].x = (int)((v4[1] + v4[2]) / 2.0 + 0.5);
    }

    for (s = 0; s < 4; s++) {
        free(xs[s]);
        free(snap[s]);
    }
    free(root);
    free(align);
    free(pos);
    free(conf);
}

/* ---------- 10. 子图边框回收 + 整体平移 ---------- */

static void remove_border_nodes(MdgGraph* g) {
    int i;
    for (i = 0; i < g->nN; i++) {
        MdgNode* nd = &g->n[i];
        int t, b, l, r, span;
        if (!nd->alive || nd->type != MDG_SUB) continue;
        if (nd->borderTop < 0 || nd->borderBottom < 0) continue;
        if (!nd->bl || !nd->br) continue;
        span = nd->maxRank - nd->minRank;
        if (span < 0) continue;
        t = nd->borderTop;
        b = nd->borderBottom;
        l = nd->bl[span];
        r = nd->br[span];
        if (l < 0 || r < 0) continue;
        nd->w = g->n[r].x - g->n[l].x;
        if (nd->w < 0) nd->w = -nd->w;
        nd->h = g->n[b].y - g->n[t].y;
        if (nd->h < 0) nd->h = -nd->h;
        nd->x = g->n[l].x + nd->w / 2;
        nd->y = g->n[t].y + nd->h / 2;
    }
    for (i = 0; i < g->nN; i++)
        if (g->n[i].alive && g->n[i].type == MDG_BORDER) g->n[i].alive = 0;
}

static void translate_graph(MdgGraph* g) {
    int i, minX = 0x7FFFFFFF, minY = 0x7FFFFFFF;
    for (i = 0; i < g->nN; i++) {
        MdgNode* nd = &g->n[i];
        int a, b;
        if (!nd->alive || nd->type == MDG_ROOT) continue;
        if (nd->type == MDG_SUB && nd->w <= 0 && nd->h <= 0) continue;
        a = nd->x - nd->w / 2;
        b = nd->y - nd->h / 2;
        if (a < minX) minX = a;
        if (b < minY) minY = b;
    }
    if (minX == 0x7FFFFFFF) minX = 0;
    if (minY == 0x7FFFFFFF) minY = 0;
    minX -= g->marginx;
    minY -= g->marginy;
    for (i = 0; i < g->nN; i++) {
        if (!g->n[i].alive) continue;
        g->n[i].x -= minX;
        g->n[i].y -= minY;
    }
}

/* ---------- 主入口 ---------- */

void mdg_layout(MdgGraph* g) {
    int i;
    Layering L;

    /* makeSpaceForEdgeLabels */
    g->ranksep /= 2;
    for (i = 0; i < g->nE; i++) g->e[i].minlen *= 2;

    mdg_acyclic(g);
    mdg_nesting(g);
    mdg_rank_simplex(g);
    remove_empty_ranks(g);
    mdg_nesting_cleanup(g);
    normalize_ranks(g);
    assign_rank_minmax(g);

    mdg_normalize(g);
    parent_dummy_chains(g);
    add_border_segments(g);
    mdg_order(g);
    layering_from_order(&L, g);
    position_y(g, &L);
    mdg_position_x(g, &L);
    layering_free(&L);
    remove_border_nodes(g);
    translate_graph(g);
}

/* ---------- 边的折点（normalize.undo 的等价物） ---------- */

/* 把调用方第 edgeId 条边被 normalize 打断出来的 dummy 链坐标交回去。
 * dagre 里这件事由 normalize.undo 做：它遍历归一化后的边，把 points 里的
 * 中间点（去掉首尾节点自身）转到 originalEdge.points 上，遇到 reversed 再
 * 反转一次。这里用 origin 字段把「被打断的边」和「调用方那条边」对上。 */
int mdg_edge_waypoints(const MdgGraph* g, int edgeId, int* xy, int maxPts) {
    int c, n = 0;
    int* tmp;
    if (!g || !xy || maxPts <= 0 || g->nN <= 0) return 0;
    tmp = (int*)mdg_alloc(sizeof(int) * (size_t)g->nN);
    for (c = 0; c < g->nChains; c++) {
        int v0 = g->chains[c], ei, v, guard = 0;
        if (v0 < 0 || v0 >= g->nN) continue;
        if (!g->n[v0].alive || g->n[v0].type != MDG_EDGE) continue;
        ei = g->n[v0].origEdge;
        if (ei < 0 || ei >= g->nE) continue;
        if (g->e[ei].origin != edgeId) continue;
        v = v0;
        while (v >= 0 && n < g->nN && guard++ < 4096) {
            int nx;
            tmp[n++] = v;
            nx = chain_next(g, v);
            if (nx < 0) break;
            /* 链尾那条边指向真实节点，不是本链的 dummy，到此为止 */
            if (g->n[nx].type != MDG_EDGE || g->n[nx].origEdge != ei) break;
            v = nx;
        }
        if (g->e[ei].reversed) {
            /* acyclic 把这条边翻成了 v->u，链的方向也跟着反了，这里还原成
             * 调用方给的方向，否则折点顺序会从终点那头开始 */
            for (v = 0; v < n / 2; v++) {
                int t = tmp[v];
                tmp[v] = tmp[n - 1 - v];
                tmp[n - 1 - v] = t;
            }
        }
        if (n > maxPts) n = maxPts;
        for (v = 0; v < n; v++) {
            xy[v * 2] = g->n[tmp[v]].x;
            xy[v * 2 + 1] = g->n[tmp[v]].y;
        }
        free(tmp);
        return n;
    }
    free(tmp);
    return 0;
}
