/* merdagre.h -- 分层布局算法（dagre C 移植）对外接口 */

#ifndef MERDAGRE_H
#define MERDAGRE_H

/* 节点种类 */
#define MDG_REAL 0   /* 普通节点 */
#define MDG_SUB 1    /* 子图（compound cluster，无尺寸） */
#define MDG_BORDER 2 /* 子图上下左右边界虚拟节点 */
#define MDG_EDGE 3   /* 长边打断产生的虚拟节点 */
#define MDG_ROOT 4   /* nesting-graph 的虚拟根 */

typedef struct {
    int idx;
    int type;
    int w, h;  /* 输入：节点尺寸；MDG_SUB 输出：子图框宽高 */
    int x, y;  /* 输出：节点中心；MDG_SUB 输出：框中心 */
    int rank;  /* 输出：所在层 */
    int order; /* 输出：层内序号 */
    int parent; /* 输入：所属子图节点索引，-1 表示游离 */
    int borderTop, borderBottom;
    int minRank, maxRank;
    int borderType; /* MDG_BORDER：0 上下边框，1 左边框，2 右边框 */
    int* bl;        /* 左边框节点，按 rank-minRank 索引 */
    int* br;        /* 右边框节点，按 rank-minRank 索引 */
    int origEdge;   /* MDG_EDGE：对应的原始边索引，-1 表示无 */
    int alive;
} MdgNode;

typedef struct {
    int u, v;
    int weight;
    int minlen;
    int reversed;
    int nesting; /* nesting-graph 加的约束边 */
    int origin;  /* 最初 mdg_add_edge* 返回的边号；acyclic 反向时沿用，用来把
                    打断后的 dummy 链还原回调用方那条边 */
    int alive;
} MdgEdge;

typedef struct {
    MdgNode* n;
    int nN, capN;
    MdgEdge* e;
    int nE, capE;
    int nodesep;
    int ranksep;
    int edgesep;
    int marginx, marginy;
    int nodeRankFactor;
    int nestingRoot;
    int maxRank;
    int* chains; /* normalize 产生的 dummy 链首节点 */
    int nChains, capChains;
} MdgGraph;

void mdg_init(MdgGraph* g);
void mdg_free(MdgGraph* g);
int mdg_add_node(MdgGraph* g, int w, int h, int parent);
int mdg_add_edge(MdgGraph* g, int u, int v);
int mdg_add_edge_ex(MdgGraph* g, int u, int v, int weight, int minlen, int nesting);
void mdg_layout(MdgGraph* g);

/* 调试：导出某节点被判定为哪一层 */
int mdg_node_rank(const MdgGraph* g, int i);

/* 取调用方第 edgeId 条边（mdg_add_edge* 的返回序号）的中间折点坐标，单位与
 * 节点 x/y 相同。xy 按 (x,y) 交替存放，最多写 maxPts 个点。返回实际点数；
 * 0 表示这条边没有跨层（不需要折点）。须在 mdg_layout 之后调用。 */
int mdg_edge_waypoints(const MdgGraph* g, int edgeId, int* xy, int maxPts);

#endif /* MERDAGRE_H */
