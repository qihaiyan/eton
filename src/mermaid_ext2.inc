/* ================================================================
   mermaid 扩展图族 · 二：state / class / er 解析（复用流程图内核）
   与全部扩展图族的布局、绘制及 Ext* 分发。
   由 mermaid.c 末尾 include。
   ================================================================ */

static int StateAdd(FlowCtx* c, const wchar_t* id, const wchar_t* label, MerShape shape) {
    int ex = FlowFind(c, id);
    if (ex >= 0) {
        if (label[0]) wcscpy_s(c->ns[ex].label, 256, label);
        return ex;
    }
    c->ns = (MerNode*)MerGrow(c->ns, &c->capN, c->nN + 1, sizeof(MerNode));
    MerNode* n2 = &c->ns[c->nN];
    ZeroMemory(n2, sizeof(*n2));
    wcscpy_s(n2->id, 64, id);
    wcscpy_s(n2->label, 256, label[0] ? label : id);
    n2->shape = shape;
    n2->sub = c->curSub;
    n2->rank = 0;
    return c->nN++;
}

static void StatePostFix(FlowCtx* c) {
    int nEnd = 0;
    for (int i = 0; i < c->nN; i++) {
        if (wcscmp(c->ns[i].id, L"__start") == 0) {
            c->ns[i].shape = MER_START;
            wcscpy(c->ns[i].label, L"");
        } else if (wcsncmp(c->ns[i].id, L"__end", 5) == 0) {
            c->ns[i].shape = MER_END;
            wcscpy(c->ns[i].label, L"");
            nEnd++;
        } else if (c->ns[i].shape == MER_RECT) {
            c->ns[i].shape = MER_ROUND;
        }
    }
    (void)nEnd;
}

static void StateParseLine(FlowCtx* c, wchar_t* line) {
    wchar_t* s = TrimW(line);
    if (!*s || wcsncmp(s, L"%%", 2) == 0) return;
    if (StartsWithWord(s, L"note") || StartsWithWord(s, L"direction") ||
        StartsWithWord(s, L"classDef") || StartsWithWord(s, L"class")) return;
    if (wcscmp(s, L"}") == 0) { c->curSub = -1; return; }

    if (StartsWithWord(s, L"state")) {
        wchar_t* rest = TrimW(s + 5);
        if (rest[0] == L'"') {                       /* state "长描述" as id */
            wchar_t* q = wcschr(rest + 1, L'"');
            if (q) {
                *q = 0;
                wchar_t* as = TrimW(q + 1);
                if (_wcsnicmp(as, L"as ", 3) == 0) {
                    StateAdd(c, TrimW(as + 3), rest + 1, MER_ROUND);
                    return;
                }
            }
        }
        wchar_t* br = wcschr(rest, L'{');            /* 复合状态 */
        if (br) {
            *br = 0;
            wchar_t* name = TrimW(rest);
            c->ss = (MerSub*)MerGrow(c->ss, &c->capS, c->nS + 1, sizeof(MerSub));
            MerSub* sub = &c->ss[c->nS++];
            ZeroMemory(sub, sizeof(*sub));
            wcsncpy(sub->title, name, 159);
            c->curSub = c->nS - 1;
            return;
        }
        StateAdd(c, TrimW(rest), L"", MER_ROUND);
        return;
    }

    /* [*] → 内部 id：行首为 __start，其后为唯一 __endN */
    wchar_t buf[512];
    int bi = 0, nEnd = 0, firstTok = 1;
    for (int i = 0; s[i] && bi < 500; i++) {
        if (s[i] == L'[' && s[i+1] == L'*' && s[i+2] == L']') {
            if (firstTok) { wcscpy(buf + bi, L"__start"); bi += 7; }
            else { _snwprintf(buf + bi, 60, L"__end%d", nEnd++); bi = (int)wcslen(buf); }
            i += 2;
        } else {
            if (s[i] != L' ' && s[i] != L'\t' && s[i] != L'-') firstTok = 0;
            buf[bi++] = s[i];
        }
    }
    buf[bi] = 0;
    FlowParseLine(c, buf);
}

/* ---------------- class ---------------- */
static const struct { const wchar_t* op; MerRel rel; BOOL markA; BOOL dash; } kClsOps[] = {
    { L"<|--", MRE_INHERIT, TRUE,  FALSE },
    { L"--|>", MRE_INHERIT, FALSE, FALSE },
    { L"..|>", MRE_INHERIT, FALSE, TRUE },
    { L"<|..", MRE_INHERIT, TRUE,  TRUE },
    { L"*--",  MRE_COMPOSE, TRUE,  FALSE },
    { L"--*",  MRE_COMPOSE, FALSE, FALSE },
    { L"o--",  MRE_AGGREG,  TRUE,  FALSE },
    { L"--o",  MRE_AGGREG,  FALSE, FALSE },
    { L"<--",  MRE_ASSOC,   TRUE,  FALSE },
    { L"-->",  MRE_ASSOC,   FALSE, FALSE },
    { L"<..",  MRE_DEPEND,  TRUE,  TRUE },
    { L"..>",  MRE_DEPEND,  FALSE, TRUE },
    { L"--",   MRE_PLAIN,   FALSE, FALSE },
    { L"..",   MRE_PLAIN,   FALSE, TRUE },
};

static void ClsExtract(const wchar_t* side, wchar_t* card, wchar_t* id) {
    wchar_t tok[256];
    wcsncpy(tok, side, 255);
    tok[255] = 0;
    card[0] = 0; id[0] = 0;
    wchar_t* p = TrimW(tok);
    wchar_t* q = wcschr(p, L'"');
    if (q) {
        *q = 0;
        wchar_t* q2 = wcschr(q + 1, L'"');
        if (q2) {
            *q2 = 0;
            wcsncpy(card, q + 1, 15);
            p = TrimW(q2 + 1);
        }
    }
    wcsncpy(id, TrimW(p), 63);
}

static void ClsParseLine(FlowCtx* c, wchar_t* s) {
    int best = -1; const wchar_t* bestPos = NULL;
    for (int k = 0; k < (int)(sizeof(kClsOps) / sizeof(kClsOps[0])); k++) {
        const wchar_t* hit = wcsstr(s, kClsOps[k].op);
        if (hit && (!bestPos || hit < bestPos)) { bestPos = hit; best = k; }
    }
    if (best < 0) return;
    wchar_t left[256], right[256];
    int ll = (int)(bestPos - s);
    wcsncpy(left, s, ll < 255 ? ll : 255);
    left[ll < 255 ? ll : 255] = 0;
    wcscpy_s(right, 256, bestPos + wcslen(kClsOps[best].op));
    wchar_t* colon = wcschr(right, L':');
    if (colon) *colon = 0;
    wchar_t cardA[16], idA[64], cardB[16], idB[64];
    ClsExtract(left, cardA, idA);
    ClsExtract(right, cardB, idB);
    if (!idA[0] || !idB[0]) return;
    int a = StateAdd(c, idA, idA, MER_RECT);
    int b = StateAdd(c, idB, idB, MER_RECT);
    c->es = (MerEdge*)MerGrow(c->es, &c->capE, c->nE + 1, sizeof(MerEdge));
    MerEdge* e = &c->es[c->nE++];
    ZeroMemory(e, sizeof(*e));
    e->a = a; e->b = b;
    e->rel = kClsOps[best].rel;
    e->markA = kClsOps[best].markA;
    e->dash = kClsOps[best].dash;
    e->arrow = FALSE;
    wcscpy_s(e->cardA, 16, cardA);
    wcscpy_s(e->cardB, 16, cardB);
    if (colon) wcsncpy(e->label, TrimW(colon + 1), 159);
}

static BOOL ClassParse(FlowCtx* c, const char* src, int len, int pos) {
    wchar_t line[1024];
    int inClass = 0, curIdx = -1;
    wchar_t* mem = NULL; int nMem = 0, methStart = -1;
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (inClass) {
            if (wcscmp(s, L"}") == 0) {
                if (curIdx >= 0 && mem) {
                    c->ns[curIdx].extra = mem;
                    c->ns[curIdx].nExtraA = methStart < 0 ? nMem : methStart;
                    c->ns[curIdx].nExtraB = methStart < 0 ? 0 : nMem - methStart;
                }
                inClass = 0; curIdx = -1; mem = NULL; nMem = 0; methStart = -1;
                continue;
            }
            if (!*s) continue;
            if (methStart < 0 && wcschr(s, L'(')) methStart = nMem;
            wchar_t* nm = (wchar_t*)realloc(mem, ((size_t)nMem + 1) * 96 * sizeof(wchar_t));
            if (!nm) continue;
            mem = nm;
            wcsncpy(mem + (size_t)nMem * 96, s, 95);
            mem[(size_t)nMem * 96 + 95] = 0;
            nMem++;
            continue;
        }
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (StartsWithWord(s, L"note") || StartsWithWord(s, L"classDef") ||
            StartsWithWord(s, L"direction")) continue;
        if (StartsWithWord(s, L"class")) {
            wchar_t* rest = TrimW(s + 5);
            wchar_t* br = wcschr(rest, L'{');
            if (br) *br = 0;
            wchar_t* name = TrimW(rest);
            wchar_t* col2 = wcschr(name, L':');
            if (col2) *col2 = 0;
            if (!*name) continue;
            int idx = StateAdd(c, name, name, MER_RECT);
            if (br) {
                inClass = 1; curIdx = idx;
                free(c->ns[idx].extra);
                c->ns[idx].extra = NULL;
                c->ns[idx].nExtraA = c->ns[idx].nExtraB = 0;
                mem = NULL; nMem = 0; methStart = -1;
            }
            continue;
        }
        ClsParseLine(c, s);
    }
    if (inClass && curIdx >= 0 && mem) {
        c->ns[curIdx].extra = mem;
        c->ns[curIdx].nExtraA = methStart < 0 ? nMem : methStart;
        c->ns[curIdx].nExtraB = methStart < 0 ? 0 : nMem - methStart;
    }
    return c->nN > 0;
}

/* ---------------- er ---------------- */
static BOOL ErParse(FlowCtx* c, const char* src, int len, int pos) {
    wchar_t line[1024];
    int inEnt = 0, curIdx = -1;
    wchar_t* mem = NULL; int nMem = 0;
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (inEnt) {
            if (wcscmp(s, L"}") == 0) {
                if (curIdx >= 0 && mem) {
                    c->ns[curIdx].extra = mem;
                    c->ns[curIdx].nExtraA = nMem;
                    c->ns[curIdx].nExtraB = 0;
                }
                inEnt = 0; curIdx = -1; mem = NULL; nMem = 0;
                continue;
            }
            wchar_t* nm = (wchar_t*)realloc(mem, ((size_t)nMem + 1) * 96 * sizeof(wchar_t));
            if (!nm) continue;
            mem = nm;
            wcsncpy(mem + (size_t)nMem * 96, s, 95);
            mem[(size_t)nMem * 96 + 95] = 0;
            nMem++;
            continue;
        }
        /* 关系行优先（A ||--o{ B : label 的 o{/|{ 含 '{'，不能当实体块头） */
        wchar_t a[64] = L"", b[64] = L"", card[24] = L"";
        if (swscanf(s, L"%63s %23s %63s", a, card, b) == 3 && wcsstr(card, L"--")) {
            wchar_t label[96] = L"";
            wchar_t* colon = wcschr(s, L':');
            if (colon) wcsncpy(label, TrimW(colon + 1), 95);
            wchar_t ca[16] = L"", cb[16] = L"";
            wchar_t* dash2 = wcsstr(card, L"--");
            if (dash2) {
                *dash2 = 0;
                wcsncpy(ca, card, 15);
                wcsncpy(cb, dash2 + 2, 15);
            }
            int ia = StateAdd(c, a, a, MER_RECT);
            int ib = StateAdd(c, b, b, MER_RECT);
            c->es = (MerEdge*)MerGrow(c->es, &c->capE, c->nE + 1, sizeof(MerEdge));
            MerEdge* e = &c->es[c->nE++];
            ZeroMemory(e, sizeof(*e));
            e->a = ia; e->b = ib;
            e->rel = MRE_PLAIN; e->arrow = FALSE;
            wcscpy_s(e->cardA, 16, ca);
            wcscpy_s(e->cardB, 16, cb);
            wcsncpy(e->label, label, 95);
            continue;
        }
        /* 实体块头：行尾是 '{' */
        if (s[wcslen(s) - 1] == L'{') {
            wchar_t name[64];
            int nl = (int)(wcslen(s) - 1);
            wcsncpy(name, s, nl < 63 ? nl : 63);
            name[nl < 63 ? nl : 63] = 0;
            int idx = StateAdd(c, TrimW(name), TrimW(name), MER_RECT);
            inEnt = 1; curIdx = idx;
            mem = NULL; nMem = 0;
            continue;
        }
    }
    return c->nN > 0;
}

/* class/er 三格框尺寸（在流程图布局前算好 wFix/hFix） */
static void ClassErFixSizeReal(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    int lineH = FontH(hdc, f->sm) + UI_Scale(2);
    for (int i = 0; i < d->nNodes; i++) {
        MerNode* nd = &d->nodes[i];
        if (!nd->extra) continue;
        int w = TextW(hdc, f->bold, nd->label) + UI_Scale(24);
        wchar_t (*mem)[96] = (wchar_t(*)[96])nd->extra;
        int total = nd->nExtraA + nd->nExtraB;
        for (int m = 0; m < total; m++) {
            int mw = TextW(hdc, f->sm, mem[m]) + UI_Scale(20);
            if (mw > w) w = mw;
        }
        if (w < UI_Scale(80)) w = UI_Scale(80);
        nd->wFix = w;
        nd->hFix = lineH + UI_Scale(8) + total * lineH + UI_Scale(6);
        if (nd->hFix < UI_Scale(30)) nd->hFix = UI_Scale(30);
    }
}

/* ================================================================ */
/*                          布局与绘制                              */
/* ================================================================ */

/* ---------------- pie ---------------- */
static void PieLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    PieD* p = (PieD*)d->ext;
    int titleH = p->title[0] ? FontH(hdc, f->bold) + UI_Scale(10) : 0;
    int R = UI_Scale(150);
    int lgW = 0;
    for (int i = 0; i < p->n; i++) {
        int w = TextW(hdc, f->sm, p->s[i].label) + UI_Scale(60);
        if (w > lgW) lgW = w;
    }
    if (lgW < UI_Scale(140)) lgW = UI_Scale(140);
    if (lgW > UI_Scale(340)) lgW = UI_Scale(340);
    d->sz.cx = UI_Scale(12) + R * 2 + UI_Scale(30) + lgW + UI_Scale(12);
    d->sz.cy = UI_Scale(12) + titleH + R * 2 + UI_Scale(12);
    (void)hdc;
}

static void PieDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    PieD* p = (PieD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int titleH = p->title[0] ? FontH(hdc, f->bold) + UI_Scale(10) : 0;
    int R = UI_Scale(150);
    int cx = ox + UI_Scale(12) + R;
    int cy = oy + UI_Scale(12) + titleH + R;
    if (p->title[0]) {
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOutW(hdc, cx, oy + UI_Scale(6), p->title, (int)wcslen(p->title));
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        SelectObject(hdc, of);
    }
    HPEN sep = CreatePen(PS_SOLID, 2, th->bg);
    HPEN op = (HPEN)SelectObject(hdc, sep);
    double a0 = -90.0;
    for (int i = 0; i < p->n; i++) {
        double frac2 = p->s[i].val / p->total;
        double a1 = a0 + frac2 * 360.0;
        HBRUSH br = CreateSolidBrush(MerPal(th, i));
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        Pie(hdc, cx - R, cy - R, cx + R, cy + R,
            (int)(cx + R * 100 * cos(a0 * 3.14159265 / 180.0)),
            (int)(cy + R * 100 * sin(a0 * 3.14159265 / 180.0)),
            (int)(cx + R * 100 * cos(a1 * 3.14159265 / 180.0)),
            (int)(cy + R * 100 * sin(a1 * 3.14159265 / 180.0)));
        SelectObject(hdc, ob);
        DeleteObject(br);
        /* 百分比 */
        if (frac2 > 0.04) {
            double mid = (a0 + a1) / 2 * 3.14159265 / 180.0;
            wchar_t txt[16];
            _snwprintf(txt, 15, L"%.0f%%", frac2 * 100);
            SetTextColor(hdc, RGB(255, 255, 255));
            HFONT of = (HFONT)SelectObject(hdc, f->sm);
            SetTextAlign(hdc, TA_CENTER | TA_TOP);
            TextOutW(hdc, (int)(cx + R * 0.62 * cos(mid)), (int)(cy + R * 0.62 * sin(mid)) - FontH(hdc, f->sm) / 2,
                     txt, (int)wcslen(txt));
            SetTextAlign(hdc, TA_LEFT | TA_TOP);
            SelectObject(hdc, of);
        }
        a0 = a1;
    }
    SelectObject(hdc, op);
    DeleteObject(sep);
    /* 图例 */
    int lx = cx + R + UI_Scale(30);
    int ly = cy - (p->n * (FontH(hdc, f->sm) + UI_Scale(8))) / 2;
    for (int i = 0; i < p->n; i++) {
        RECT sw = { lx, ly + UI_Scale(2), lx + UI_Scale(14), ly + UI_Scale(14) };
        HBRUSH br = CreateSolidBrush(MerPal(th, i));
        FillRect(hdc, &sw, br);
        DeleteObject(br);
        SetTextColor(hdc, th->fg);
        wchar_t txt[160];
        _snwprintf(txt, 159, L"%s  (%.0f%%)", p->s[i].label, p->s[i].val / p->total * 100);
        HFONT of = (HFONT)SelectObject(hdc, f->sm);
        TextOutW(hdc, lx + UI_Scale(20), ly, txt, (int)wcslen(txt));
        SelectObject(hdc, of);
        ly += FontH(hdc, f->sm) + UI_Scale(8);
    }
}

/* ---------------- quadrant ---------------- */
static RECT RectMake4(int l, int t, int r, int b) {
    RECT rc = { l, t, r, b };
    return rc;
}

static void QuadLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    (void)f;
    QuadD* p = (QuadD*)d->ext;
    int S = UI_Scale(360);
    int extra = 0;
    if (p->title[0]) extra += FontH(hdc, f->bold) + UI_Scale(10);
    d->sz.cx = UI_Scale(40) + S + UI_Scale(20);
    d->sz.cy = UI_Scale(10) + extra + S + UI_Scale(40);
}

static void QuadDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    QuadD* p = (QuadD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int S = UI_Scale(360);
    int top = oy + UI_Scale(10);
    if (p->title[0]) {
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOutW(hdc, ox + UI_Scale(40) + S / 2, top, p->title, (int)wcslen(p->title));
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        SelectObject(hdc, of);
        top += FontH(hdc, f->bold) + UI_Scale(10);
    }
    int L = ox + UI_Scale(40), T = top, R2 = L + S, B2 = T + S;
    /* 象限底色 */
    HBRUSH q1 = CreateSolidBrush(th->tableHeadBg);
    RECT rq = { L, T, R2, B2 };
    FillRect(hdc, &rq, q1);
    DeleteObject(q1);
    HPEN grid = CreatePen(PS_SOLID, 1, th->frame);
    HPEN op = (HPEN)SelectObject(hdc, grid);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, L, T, R2, B2);
    MoveToEx(hdc, (L + R2) / 2, T, NULL); LineTo(hdc, (L + R2) / 2, B2);
    MoveToEx(hdc, L, (T + B2) / 2, NULL); LineTo(hdc, R2, (T + B2) / 2);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(grid);
    /* 象限标签（右上=1，左上=2，左下=3，右下=4） */
    SetTextColor(hdc, th->fgMuted);
    HFONT of = (HFONT)SelectObject(hdc, f->sm);
    int qw = S / 2;
    RECT qrc;
    qrc = RectMake4(L + qw, T, R2, T + qw);          DrawTextW(hdc, p->q[0], -1, &qrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    qrc = RectMake4(L, T, L + qw, T + qw);           DrawTextW(hdc, p->q[1], -1, &qrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    qrc = RectMake4(L, T + qw, L + qw, B2);          DrawTextW(hdc, p->q[2], -1, &qrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    qrc = RectMake4(L + qw, T + qw, R2, B2);         DrawTextW(hdc, p->q[3], -1, &qrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    /* 轴标签 */
    TextOutW(hdc, L, T - FontH(hdc, f->sm) - 2, p->yhi, (int)wcslen(p->yhi));
    TextOutW(hdc, L, B2 + 4, p->ylo, (int)wcslen(p->ylo));
    TextOutW(hdc, L, B2 + UI_Scale(18), p->xlo, (int)wcslen(p->xlo));
    {
        int w2 = TextW(hdc, f->sm, p->xhi);
        TextOutW(hdc, R2 - w2, B2 + UI_Scale(18), p->xhi, (int)wcslen(p->xhi));
    }
    /* 数据点 */
    HBRUSH ptBr = CreateSolidBrush(th->link);
    for (int i = 0; i < p->n; i++) {
        double x = p->pts[i].x, y = p->pts[i].y;
        if (x < 0) x = 0; if (x > 1) x = 1;
        if (y < 0) y = 0; if (y > 1) y = 1;
        int px = (int)(L + x * S), py = (int)(B2 - y * S);
        HBRUSH ob2 = (HBRUSH)SelectObject(hdc, ptBr);
        HPEN np = CreatePen(PS_SOLID, 1, th->bg);
        HPEN op2 = (HPEN)SelectObject(hdc, np);
        Ellipse(hdc, px - UI_Scale(5), py - UI_Scale(5), px + UI_Scale(5), py + UI_Scale(5));
        SelectObject(hdc, op2);
        SelectObject(hdc, ob2);
        DeleteObject(np);
        SetTextColor(hdc, th->fg);
        TextOutW(hdc, px + UI_Scale(8), py - FontH(hdc, f->sm) / 2,
                 p->pts[i].name, (int)wcslen(p->pts[i].name));
    }
    DeleteObject(ptBr);
    SelectObject(hdc, of);
}
