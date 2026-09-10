/* ================================================================
   mermaid 扩展图族 · 三：timeline / journey / gantt / xychart /
   mindmap / gitGraph 的布局绘制，及 Ext* 总分发。
   由 mermaid.c 末尾 include。
   ================================================================ */

/* ---------------- timeline ---------------- */
static void TlLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    TlD* p = (TlD*)d->ext;
    int y = UI_Scale(12);
    if (p->title[0]) y += FontH(hdc, f->bold) + UI_Scale(10);
    int maxW = 0;
    for (int i = 0; i < p->n; i++) {
        y += UI_Scale(8);
        y += FontH(hdc, f->bold) + UI_Scale(6);
        for (int j = 0; j < p->secs[i].n; j++) {
            int w = TextW(hdc, f->body, p->secs[i].ev[j].head);
            if (p->secs[i].ev[j].body[0]) {
                int w2 = TextW(hdc, f->sm, p->secs[i].ev[j].body);
                if (w2 > w) w = w2;
                y += FontH(hdc, f->sm) + UI_Scale(2);
            }
            if (w > maxW) maxW = w;
            y += FontH(hdc, f->body) + UI_Scale(10);
        }
    }
    d->sz.cx = UI_Scale(24) + UI_Scale(60) + maxW + UI_Scale(30);
    d->sz.cy = y + UI_Scale(10);
}

static void TlDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    TlD* p = (TlD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int y = oy + UI_Scale(12);
    if (p->title[0]) {
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        TextOutW(hdc, ox + UI_Scale(24), y, p->title, (int)wcslen(p->title));
        SelectObject(hdc, of);
        y += FontH(hdc, f->bold) + UI_Scale(10);
    }
    int spineX = ox + UI_Scale(24) + UI_Scale(6);
    int totalH = 0;
    for (int i = 0; i < p->n; i++) {
        totalH += UI_Scale(8) + FontH(hdc, f->bold) + UI_Scale(6);
        for (int j = 0; j < p->secs[i].n; j++) {
            totalH += FontH(hdc, f->body) + UI_Scale(10);
            if (p->secs[i].ev[j].body[0]) totalH += FontH(hdc, f->sm) + UI_Scale(2);
        }
    }
    HPEN spine = CreatePen(PS_SOLID, UI_Scale(3), th->frame);
    HPEN op = (HPEN)SelectObject(hdc, spine);
    MoveToEx(hdc, spineX, y, NULL);
    LineTo(hdc, spineX, y + totalH);
    SelectObject(hdc, op);
    DeleteObject(spine);
    HBRUSH dotBr = CreateSolidBrush(th->link);
    for (int i = 0; i < p->n; i++) {
        y += UI_Scale(8);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, dotBr);
        HPEN np = CreatePen(PS_SOLID, 1, th->bg);
        HPEN op2 = (HPEN)SelectObject(hdc, np);
        Ellipse(hdc, spineX - UI_Scale(5), y, spineX + UI_Scale(5), y + UI_Scale(10));
        SelectObject(hdc, op2);
        SelectObject(hdc, ob);
        DeleteObject(np);
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        TextOutW(hdc, spineX + UI_Scale(16), y, p->secs[i].title, (int)wcslen(p->secs[i].title));
        SelectObject(hdc, of);
        y += FontH(hdc, f->bold) + UI_Scale(6);
        for (int j = 0; j < p->secs[i].n; j++) {
            TlEv* ev = &p->secs[i].ev[j];
            HFONT of2 = (HFONT)SelectObject(hdc, f->body);
            SetTextColor(hdc, th->fg);
            TextOutW(hdc, spineX + UI_Scale(30), y, ev->head, (int)wcslen(ev->head));
            SelectObject(hdc, of2);
            y += FontH(hdc, f->body);
            if (ev->body[0]) {
                SetTextColor(hdc, th->fgMuted);
                HFONT of3 = (HFONT)SelectObject(hdc, f->sm);
                TextOutW(hdc, spineX + UI_Scale(30), y, ev->body, (int)wcslen(ev->body));
                SelectObject(hdc, of3);
                y += FontH(hdc, f->sm) + UI_Scale(2);
            }
            y += UI_Scale(10);
        }
    }
    DeleteObject(dotBr);
}

/* ---------------- journey ---------------- */
static COLORREF JrnScoreColor(const MdTheme* th, int score) {
    int dark = (GetRValue(th->bg) + GetGValue(th->bg) + GetBValue(th->bg)) < 384;
    if (score >= 4) return dark ? RGB(108,168,94) : RGB(84,152,66);
    if (score == 3) return dark ? RGB(196,166,80) : RGB(178,138,40);
    if (score == 2) return dark ? RGB(212,140,80) : RGB(196,120,50);
    return dark ? RGB(212,96,100) : RGB(200,70,74);
}

static void JrnLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    JrnD* p = (JrnD*)d->ext;
    int colW = UI_Scale(240);
    int y = UI_Scale(12);
    if (p->title[0]) y += FontH(hdc, f->bold) + UI_Scale(10);
    int maxColH = 0;
    for (int i = 0; i < p->n; i++) {
        int h = FontH(hdc, f->bold) + UI_Scale(10);
        for (int j = 0; j < p->secs[i].n; j++) {
            h += UI_Scale(28) + UI_Scale(4);
            if (p->secs[i].t[j].actors[0]) h += FontH(hdc, f->sm) + UI_Scale(2);
        }
        if (h > maxColH) maxColH = h;
    }
    d->sz.cx = UI_Scale(12) + p->n * (colW + UI_Scale(16)) + UI_Scale(12);
    d->sz.cy = y + maxColH + UI_Scale(12);
}

static void JrnDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    JrnD* p = (JrnD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int colW = UI_Scale(240);
    int y0 = oy + UI_Scale(12);
    if (p->title[0]) {
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        TextOutW(hdc, ox + UI_Scale(12), y0, p->title, (int)wcslen(p->title));
        SelectObject(hdc, of);
        y0 += FontH(hdc, f->bold) + UI_Scale(10);
    }
    for (int i = 0; i < p->n; i++) {
        int cx = ox + UI_Scale(12) + i * (colW + UI_Scale(16));
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        int nw = TextW(hdc, f->bold, p->secs[i].name);
        TextOutW(hdc, cx + (colW - nw) / 2, y0, p->secs[i].name, (int)wcslen(p->secs[i].name));
        SelectObject(hdc, of);
        int y = y0 + FontH(hdc, f->bold) + UI_Scale(10);
        for (int j = 0; j < p->secs[i].n; j++) {
            JTask* t = &p->secs[i].t[j];
            int cy = y + UI_Scale(12);
            HBRUSH br = CreateSolidBrush(JrnScoreColor(th, t->score));
            HPEN np = CreatePen(PS_SOLID, 1, th->bg);
            HPEN op = (HPEN)SelectObject(hdc, np);
            HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
            Ellipse(hdc, cx, cy - UI_Scale(11), cx + UI_Scale(22), cy + UI_Scale(11));
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(np);
            DeleteObject(br);
            wchar_t num[8];
            _snwprintf(num, 7, L"%d", t->score);
            SetTextColor(hdc, RGB(255, 255, 255));
            HFONT of2 = (HFONT)SelectObject(hdc, f->bold);
            SetTextAlign(hdc, TA_CENTER | TA_TOP);
            TextOutW(hdc, cx + UI_Scale(11), cy - FontH(hdc, f->bold) / 2, num, (int)wcslen(num));
            SetTextAlign(hdc, TA_LEFT | TA_TOP);
            SelectObject(hdc, of2);
            HFONT of3 = (HFONT)SelectObject(hdc, f->sm);
            SetTextColor(hdc, th->fg);
            RECT trc = RectMake4(cx + UI_Scale(28), cy - FontH(hdc, f->sm) / 2,
                                  cx + colW, cy + FontH(hdc, f->sm));
            DrawTextW(hdc, t->task, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += UI_Scale(24) + UI_Scale(4);
            if (t->actors[0]) {
                SetTextColor(hdc, th->fgMuted);
                RECT arc = RectMake4(cx + UI_Scale(28), y, cx + colW, y + FontH(hdc, f->sm) + 2);
                DrawTextW(hdc, t->actors, -1, &arc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += FontH(hdc, f->sm) + UI_Scale(2);
            }
            y += UI_Scale(6);
            SelectObject(hdc, of3);
        }
    }
}

/* ---------------- gantt ---------------- */
static void GanLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    GanD* p = (GanD*)d->ext;
    int rowH = UI_Scale(26);
    int y = UI_Scale(12);
    if (p->title[0]) y += FontH(hdc, f->bold) + UI_Scale(10);
    int axisH = UI_Scale(24);
    long days = p->maxD - p->minD + 1;
    int plotW = (int)(days * UI_Scale(11));
    if (plotW < UI_Scale(300)) plotW = UI_Scale(300);
    int nSec = 0;
    for (int i = 0; i < p->nsecs; i++)
        if (p->secs[i].name[0]) nSec++;
    d->sz.cx = UI_Scale(12) + UI_Scale(170) + plotW + UI_Scale(12);
    d->sz.cy = y + axisH + (p->n + nSec) * rowH + UI_Scale(12);
}

static void GanDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    GanD* p = (GanD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int rowH = UI_Scale(26);
    int leftW = UI_Scale(170);
    int y = oy + UI_Scale(12);
    if (p->title[0]) {
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        TextOutW(hdc, ox + UI_Scale(12), y, p->title, (int)wcslen(p->title));
        SelectObject(hdc, of);
        y += FontH(hdc, f->bold) + UI_Scale(10);
    }
    int axisH = UI_Scale(24);
    long days = p->maxD - p->minD + 1;
    int plotW = (int)(days * UI_Scale(11));
    if (plotW < UI_Scale(300)) plotW = UI_Scale(300);
    int L = ox + UI_Scale(12) + leftW;
    int T = y + axisH;
    double pxPerDay = (double)plotW / days;
    int totalRows = p->n + p->nsecs;
    /* 月份刻度与竖向网格 */
    SetTextColor(hdc, th->fgMuted);
    HFONT sf = (HFONT)SelectObject(hdc, f->sm);
    HPEN grid = CreatePen(PS_SOLID, 1, th->tableLine);
    HPEN op = (HPEN)SelectObject(hdc, grid);
    {
        int yy, mm, dd;
        DaysToDate(p->minD, &yy, &mm, &dd);
        long cur = DateToDays(yy, mm, 1);
        while (cur <= p->maxD) {
            int x = L + (int)((cur - p->minD) * pxPerDay);
            wchar_t lab[32];
            _snwprintf(lab, 31, L"%04d-%02d", yy, mm);
            TextOutW(hdc, x + 3, y, lab, (int)wcslen(lab));
            MoveToEx(hdc, x, T, NULL);
            LineTo(hdc, x, T + totalRows * rowH + UI_Scale(6));
            mm++;
            if (mm > 12) { mm = 1; yy++; }
            cur = DateToDays(yy, mm, 1);
        }
    }
    SelectObject(hdc, op);
    DeleteObject(grid);
    /* 任务行（节标题作为分组行内联显示） */
    int row = 0;
    int secShown = 0;
    for (int i = 0; i < p->n; i++) {
        GanTask* t = &p->t[i];
        /* 在任务开始前展示尚未展示的节标题（简化：全部前置一次展示） */
        if (secShown < p->nsecs && i == 0) {
            for (int k = 0; k < p->nsecs; k++) {
                if (!p->secs[k].name[0]) continue;
                int ry = T + row * rowH;
                SetTextColor(hdc, th->fgMuted);
                HFONT bf = (HFONT)SelectObject(hdc, f->sm);
                TextOutW(hdc, ox + UI_Scale(12), ry, p->secs[k].name, (int)wcslen(p->secs[k].name));
                SelectObject(hdc, bf);
                row++;
            }
            secShown = 1;
        }
        int ry = T + row * rowH;
        SetTextColor(hdc, th->fg);
        HFONT bf = (HFONT)SelectObject(hdc, f->sm);
        RECT nrc = RectMake4(ox + UI_Scale(12), ry, L - UI_Scale(6), ry + rowH);
        DrawTextW(hdc, t->name, -1, &nrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, bf);
        int x1 = L + (int)((t->s - p->minD) * pxPerDay);
        int x2 = L + (int)((t->e - p->minD) * pxPerDay);
        if (x2 - x1 < UI_Scale(8)) x2 = x1 + UI_Scale(8);
        COLORREF clr = t->crit ? RGB(224,108,117) :
                       t->done ? th->fgMuted :
                       t->active ? th->link : MerPal(th, 0);
        HBRUSH br = CreateSolidBrush(clr);
        RECT bar = { x1, ry + rowH / 2 - UI_Scale(7), x2, ry + rowH / 2 + UI_Scale(7) };
        FillRect(hdc, &bar, br);
        DeleteObject(br);
        row++;
    }
}

/* ---------------- xychart ---------------- */
static void XyLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    XyD* p = (XyD*)d->ext;
    int W = UI_Scale(420), H = UI_Scale(260);
    int extra = p->title[0] ? FontH(hdc, f->bold) + UI_Scale(10) : 0;
    d->sz.cx = UI_Scale(40) + W + UI_Scale(30);
    d->sz.cy = UI_Scale(10) + extra + H + UI_Scale(40);
}

static void XyDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    XyD* p = (XyD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int W = UI_Scale(420), H = UI_Scale(260);
    int top = oy + UI_Scale(10);
    if (p->title[0]) {
        SetTextColor(hdc, th->fg);
        HFONT of = (HFONT)SelectObject(hdc, f->bold);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOutW(hdc, ox + UI_Scale(40) + W / 2, top, p->title, (int)wcslen(p->title));
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        SelectObject(hdc, of);
        top += FontH(hdc, f->bold) + UI_Scale(10);
    }
    int L = ox + UI_Scale(40), T = top, R2 = L + W, B2 = T + H;
    int nCats = p->nCats;
    for (int i = 0; i < p->nSer; i++)
        if (p->ser[i].n > nCats) nCats = p->ser[i].n;
    if (nCats <= 0) nCats = 1;
    HPEN ap = CreatePen(PS_SOLID, 1, th->fgMuted);
    HPEN op = (HPEN)SelectObject(hdc, ap);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, L, T, R2, B2);
    SelectObject(hdc, ob);
    SetTextColor(hdc, th->fgMuted);
    for (int k = 0; k <= 5; k++) {
        double v = p->yMin + (p->yMax - p->yMin) * k / 5.0;
        int y = B2 - (int)((double)H * k / 5.0);
        wchar_t lab[32];
        _snwprintf(lab, 31, L"%g", v);
        int tw = TextW(hdc, f->sm, lab);
        TextOutW(hdc, L - tw - UI_Scale(6), y - FontH(hdc, f->sm) / 2, lab, (int)wcslen(lab));
        if (k > 0 && k < 5) {
            MoveToEx(hdc, L, y, NULL); LineTo(hdc, R2, y);
        }
    }
    double catW = (double)W / nCats;
    for (int c = 0; c < p->nCats && c < nCats; c++) {
        int cx = (int)(L + catW * (c + 0.5));
        int tw = TextW(hdc, f->sm, p->cats[c]);
        TextOutW(hdc, cx - tw / 2, B2 + UI_Scale(6), p->cats[c], (int)wcslen(p->cats[c]));
    }
    SelectObject(hdc, op);
    DeleteObject(ap);
    int nBar = 0;
    for (int i = 0; i < p->nSer; i++) if (p->ser[i].isBar) nBar++;
    int barIdx = 0;
    for (int i = 0; i < p->nSer; i++) {
        XYSeries* s = &p->ser[i];
        COLORREF clr = MerPal(th, i);
        if (s->isBar) {
            HBRUSH br = CreateSolidBrush(clr);
            for (int j = 0; j < s->n && j < nCats; j++) {
                double hv = (s->vals[j] - p->yMin) / (p->yMax - p->yMin);
                if (hv < 0) hv = 0;
                if (hv > 1) hv = 1;
                int bh = (int)(hv * H);
                double bw = catW * 0.6 / (nBar > 0 ? nBar : 1);
                int x = (int)(L + catW * j + catW * 0.2 + bw * barIdx);
                RECT bar = { x, B2 - bh, (int)(x + bw), B2 };
                FillRect(hdc, &bar, br);
            }
            DeleteObject(br);
            barIdx++;
        } else {
            HPEN lp = CreatePen(PS_SOLID, 2, clr);
            HPEN op2 = (HPEN)SelectObject(hdc, lp);
            for (int j = 0; j < s->n && j < nCats; j++) {
                double vv = (s->vals[j] - p->yMin) / (p->yMax - p->yMin);
                if (vv < 0) vv = 0;
                if (vv > 1) vv = 1;
                int cx = (int)(L + catW * (j + 0.5));
                int cy = B2 - (int)(vv * H);
                if (j == 0) MoveToEx(hdc, cx, cy, NULL);
                else LineTo(hdc, cx, cy);
            }
            SelectObject(hdc, op2);
            DeleteObject(lp);
            HBRUSH mb = CreateSolidBrush(clr);
            for (int j = 0; j < s->n && j < nCats; j++) {
                double vv = (s->vals[j] - p->yMin) / (p->yMax - p->yMin);
                if (vv < 0) vv = 0;
                if (vv > 1) vv = 1;
                int cx = (int)(L + catW * (j + 0.5));
                int cy = B2 - (int)(vv * H);
                HBRUSH ob2 = (HBRUSH)SelectObject(hdc, mb);
                HPEN np = CreatePen(PS_SOLID, 1, th->bg);
                HPEN op3 = (HPEN)SelectObject(hdc, np);
                Ellipse(hdc, cx - UI_Scale(4), cy - UI_Scale(4), cx + UI_Scale(4), cy + UI_Scale(4));
                SelectObject(hdc, op3);
                SelectObject(hdc, ob2);
                DeleteObject(np);
            }
            DeleteObject(mb);
        }
    }
}

/* ---------------- mindmap ---------------- */
static int MnSubtreeH(MnD* p, int idx) {
    MnNode* n = &p->ns[idx];
    int firstChild = -1, lastChildEnd = idx;
    for (int j = idx + 1; j < p->n; j++) {
        if (p->ns[j].depth <= n->depth) break;
        if (p->ns[j].depth == n->depth + 1 && firstChild < 0) firstChild = j;
        lastChildEnd = j;
    }
    if (firstChild < 0) return n->h;
    int total = 0, nc = 0;
    int j = firstChild;
    while (j <= lastChildEnd) {
        if (p->ns[j].depth == n->depth + 1) {
            total += MnSubtreeH(p, j);
            nc++;
            int k = j + 1;
            while (k <= lastChildEnd && p->ns[k].depth > n->depth + 1) k++;
            j = k;
        } else j++;
    }
    if (nc > 1) total += UI_Scale(14) * (nc - 1);
    return total > n->h ? total : n->h;
}

static void MnPlace(MnD* p, int idx, int y) {
    MnNode* n = &p->ns[idx];
    n->y = y;
    int firstChild = -1, lastChildEnd = idx;
    for (int j = idx + 1; j < p->n; j++) {
        if (p->ns[j].depth <= n->depth) break;
        if (p->ns[j].depth == n->depth + 1 && firstChild < 0) firstChild = j;
        lastChildEnd = j;
    }
    if (firstChild < 0) return;
    int subtree = MnSubtreeH(p, idx);
    int childY = y + (n->h - subtree) / 2;
    int j = firstChild;
    while (j <= lastChildEnd) {
        if (p->ns[j].depth == n->depth + 1) {
            int sh = MnSubtreeH(p, j);
            MnPlace(p, j, childY + (sh - p->ns[j].h) / 2);
            childY += sh + UI_Scale(14);
            int k = j + 1;
            while (k <= lastChildEnd && p->ns[k].depth > n->depth + 1) k++;
            j = k;
        } else j++;
    }
}

static void MnLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    MnD* p = (MnD*)d->ext;
    int padX = UI_Scale(12), padY = UI_Scale(6);
    for (int i = 0; i < p->n; i++) {
        MnNode* n = &p->ns[i];
        n->w = TextW(hdc, f->body, n->text) + padX * 2;
        n->h = FontH(hdc, f->body) + padY * 2;
        if (n->shape == MER_CIRCLE) {
            int m = n->w > n->h ? n->w : n->h;
            n->w = n->h = m;
        }
    }
    int levelW = 0;
    for (int i = 0; i < p->n; i++) {
        int need = p->ns[i].w + UI_Scale(50);
        if (need > levelW) levelW = need;
    }
    for (int i = 0; i < p->n; i++)
        p->ns[i].x = UI_Scale(12) + p->ns[i].depth * levelW;
    MnPlace(p, 0, UI_Scale(12));
    int maxX = 0, maxY = 0;
    for (int i = 0; i < p->n; i++) {
        if (p->ns[i].x + p->ns[i].w > maxX) maxX = p->ns[i].x + p->ns[i].w;
        if (p->ns[i].y + p->ns[i].h > maxY) maxY = p->ns[i].y + p->ns[i].h;
    }
    d->sz.cx = maxX + UI_Scale(12);
    d->sz.cy = maxY + UI_Scale(12);
}

static void MnDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    MnD* p = (MnD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    HPEN link = CreatePen(PS_SOLID, 2, th->frame);
    HPEN op = (HPEN)SelectObject(hdc, link);
    for (int i = 1; i < p->n; i++) {
        int par = p->ns[i].parent;
        if (par < 0) continue;
        POINT a = { ox + p->ns[par].x + p->ns[par].w,
                    oy + p->ns[par].y + p->ns[par].h / 2 };
        POINT b = { ox + p->ns[i].x,
                    oy + p->ns[i].y + p->ns[i].h / 2 };
        POINT c1 = { (a.x + b.x) / 2, a.y };
        POINT c2 = { (a.x + b.x) / 2, b.y };
        POINT pts[4] = { a, c1, c2, b };
        PolyBezier(hdc, pts, 4);
    }
    SelectObject(hdc, op);
    DeleteObject(link);
    HPEN np = CreatePen(PS_SOLID, 1, th->merNodeBorder);
    HBRUSH nb = CreateSolidBrush(th->merNodeFill);
    for (int i = 0; i < p->n; i++) {
        MnNode* n = &p->ns[i];
        int l = ox + n->x, t = oy + n->y, r = l + n->w, b = t + n->h;
        HPEN op2 = (HPEN)SelectObject(hdc, np);
        HBRUSH ob2 = (HBRUSH)SelectObject(hdc, nb);
        if (n->shape == MER_CIRCLE) Ellipse(hdc, l, t, r, b);
        else if (n->shape == MER_STADIUM) RoundRect(hdc, l, t, r, b, n->h / 2, n->h / 2);
        else Rectangle(hdc, l, t, r, b);
        SelectObject(hdc, ob2);
        SelectObject(hdc, op2);
        RECT rc = RectMake4(l + UI_Scale(4), t + UI_Scale(2), r - UI_Scale(4), b - UI_Scale(2));
        SetTextColor(hdc, th->merNodeText);
        HFONT of = (HFONT)SelectObject(hdc, i == 0 ? f->bold : f->body);
        DrawTextW(hdc, n->text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, of);
    }
    DeleteObject(np);
    DeleteObject(nb);
}

/* ---------------- gitGraph ---------------- */
static void GitLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    GitD* g = (GitD*)d->ext;
    int maxLane = 0;
    for (int i = 0; i < g->n; i++)
        if (g->cs[i].lane > maxLane) maxLane = g->cs[i].lane;
    d->sz.cx = UI_Scale(20) + (g->nSeq + 1) * UI_Scale(70) + UI_Scale(30);
    d->sz.cy = UI_Scale(20) + (maxLane + 1) * UI_Scale(50) + UI_Scale(40);
    (void)hdc;
}

static void GitDraw(MermaidDiagram* d, HDC hdc, int ox, int oy, const MdFonts* f, const MdTheme* th) {
    GitD* g = (GitD*)d->ext;
    SetBkMode(hdc, TRANSPARENT);
    int maxLane = 0;
    for (int i = 0; i < g->n; i++)
        if (g->cs[i].lane > maxLane) maxLane = g->cs[i].lane;
    int stepX = UI_Scale(70), laneH = UI_Scale(50);
#define GIT_X(seq)   (ox + UI_Scale(20) + (seq) * stepX)
#define GIT_Y(lane)  (oy + UI_Scale(20) + (lane) * laneH + laneH / 2)
    HPEN lanePen = CreatePen(PS_SOLID, 2, th->tableLine);
    HPEN op = (HPEN)SelectObject(hdc, lanePen);
    for (int l2 = 0; l2 <= maxLane; l2++) {
        MoveToEx(hdc, GIT_X(0), GIT_Y(l2), NULL);
        LineTo(hdc, GIT_X(g->nSeq), GIT_Y(l2));
    }
    SelectObject(hdc, op);
    DeleteObject(lanePen);
    for (int i = 0; i < g->n; i++) {
        int x = GIT_X(g->cs[i].seq), y = GIT_Y(g->cs[i].lane);
        int prev = -1;
        for (int j = i - 1; j >= 0; j--)
            if (g->cs[j].lane == g->cs[i].lane && g->cs[j].mergeFrom < 0) { prev = j; break; }
        HPEN cp = CreatePen(PS_SOLID, 2, MerPal(th, g->cs[i].lane));
        HPEN op2 = (HPEN)SelectObject(hdc, cp);
        if (prev >= 0) {
            MoveToEx(hdc, GIT_X(g->cs[prev].seq), GIT_Y(g->cs[prev].lane), NULL);
            LineTo(hdc, x, y);
        }
        if (g->cs[i].mergeFrom >= 0) {
            int src = -1;
            for (int j = i - 1; j >= 0; j--)
                if (g->cs[j].lane == g->cs[i].mergeFrom) { src = j; break; }
            if (src >= 0) {
                HPEN mp = CreatePen(PS_DASH, 2, MerPal(th, g->cs[i].mergeFrom));
                HPEN op3 = (HPEN)SelectObject(hdc, mp);
                MoveToEx(hdc, GIT_X(g->cs[src].seq), GIT_Y(g->cs[src].lane), NULL);
                int mx = (GIT_X(g->cs[src].seq) + x) / 2;
                LineTo(hdc, mx, GIT_Y(g->cs[src].lane));
                LineTo(hdc, mx, y);
                LineTo(hdc, x, y);
                SelectObject(hdc, op3);
                DeleteObject(mp);
            }
        }
        SelectObject(hdc, op2);
        DeleteObject(cp);
    }
    for (int i = 0; i < g->n; i++) {
        int x = GIT_X(g->cs[i].seq), y = GIT_Y(g->cs[i].lane);
        HBRUSH br = CreateSolidBrush(MerPal(th, g->cs[i].lane));
        HPEN np = CreatePen(PS_SOLID, 2, th->bg);
        HPEN op2 = (HPEN)SelectObject(hdc, np);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        Ellipse(hdc, x - UI_Scale(7), y - UI_Scale(7), x + UI_Scale(7), y + UI_Scale(7));
        SelectObject(hdc, ob);
        SelectObject(hdc, op2);
        DeleteObject(np);
        DeleteObject(br);
        SetTextColor(hdc, th->fgMuted);
        HFONT of = (HFONT)SelectObject(hdc, f->sm);
        TextOutW(hdc, x - UI_Scale(10), y + UI_Scale(9), g->cs[i].id, (int)wcslen(g->cs[i].id));
        SelectObject(hdc, of);
    }
    for (int b2 = 0; b2 < g->nb; b2++) {
        int x = GIT_X(g->bs[b2].firstSeq);
        int y = GIT_Y(g->bs[b2].lane) - UI_Scale(24);
        RECT tag = { x - UI_Scale(30), y, x + UI_Scale(60), y + UI_Scale(18) };
        HBRUSH tb = CreateSolidBrush(MerPal(th, g->bs[b2].lane));
        FillRect(hdc, &tag, tb);
        DeleteObject(tb);
        RECT trc = tag;
        InflateRect(&trc, -UI_Scale(2), 0);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT of = (HFONT)SelectObject(hdc, f->sm);
        DrawTextW(hdc, g->bs[b2].name, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, of);
    }
}

/* ---------------- Ext* 分发 ---------------- */
static BOOL ExtParse(MermaidDiagram* d, const char* src, int len, int pos) {
    wchar_t line[1024];
    switch (d->kind) {
        case 2: {   /* state */
            FlowCtx c; ZeroMemory(&c, sizeof(c));
            c.curSub = -1;
            while (pos < len) {
                if (!LineToWide(src, len, &pos, line, 1024)) continue;
                StateParseLine(&c, line);
            }
            StatePostFix(&c);
            if (c.nN == 0) { free(c.ns); free(c.es); free(c.ss); return FALSE; }
            d->nodes = c.ns; d->nNodes = c.nN;
            d->edges = c.es; d->nEdges = c.nE;
            d->subs = c.ss; d->nSubs = c.nS;
            return TRUE;
        }
        case 3: {   /* class */
            FlowCtx c; ZeroMemory(&c, sizeof(c));
            c.curSub = -1;
            if (!ClassParse(&c, src, len, pos)) {
                for (int i = 0; i < c.nN; i++) free(c.ns[i].extra);
                free(c.ns); free(c.es); free(c.ss);
                return FALSE;
            }
            d->nodes = c.ns; d->nNodes = c.nN;
            d->edges = c.es; d->nEdges = c.nE;
            d->subs = c.ss; d->nSubs = c.nS;
            return TRUE;
        }
        case 4: {   /* er */
            FlowCtx c; ZeroMemory(&c, sizeof(c));
            c.curSub = -1;
            if (!ErParse(&c, src, len, pos)) {
                for (int i = 0; i < c.nN; i++) free(c.ns[i].extra);
                free(c.ns); free(c.es); free(c.ss);
                return FALSE;
            }
            d->nodes = c.ns; d->nNodes = c.nN;
            d->edges = c.es; d->nEdges = c.nE;
            d->subs = c.ss; d->nSubs = c.nS;
            return TRUE;
        }
        case 5: d->ext = calloc(1, sizeof(PieD));
                return d->ext && PieParse((PieD*)d->ext, src, len, pos);
        case 6: d->ext = calloc(1, sizeof(QuadD));
                return d->ext && QuadParse((QuadD*)d->ext, src, len, pos);
        case 7: d->ext = calloc(1, sizeof(TlD));
                return d->ext && TlParse((TlD*)d->ext, src, len, pos);
        case 8: d->ext = calloc(1, sizeof(JrnD));
                return d->ext && JrnParse((JrnD*)d->ext, src, len, pos);
        case 9: d->ext = calloc(1, sizeof(GanD));
                return d->ext && GanParse((GanD*)d->ext, src, len, pos);
        case 10: d->ext = calloc(1, sizeof(XyD));
                 return d->ext && XyParse((XyD*)d->ext, src, len, pos);
        case 11: d->ext = calloc(1, sizeof(MnD));
                 return d->ext && MnParse((MnD*)d->ext, src, len, pos);
        case 12: d->ext = calloc(1, sizeof(GitD));
                 return d->ext && GitParse((GitD*)d->ext, src, len, pos);
    }
    return FALSE;
}

static void ExtFree(MermaidDiagram* d) {
    switch (d->kind) {
        case 5: { PieD* p = (PieD*)d->ext; if (p) free(p->s); break; }
        case 6: { QuadD* p = (QuadD*)d->ext; if (p) free(p->pts); break; }
        case 7: {
            TlD* p = (TlD*)d->ext;
            if (p) { for (int i = 0; i < p->n; i++) free(p->secs[i].ev); free(p->secs); }
            break;
        }
        case 8: {
            JrnD* p = (JrnD*)d->ext;
            if (p) { for (int i = 0; i < p->n; i++) free(p->secs[i].t); free(p->secs); }
            break;
        }
        case 9: { GanD* p = (GanD*)d->ext; if (p) { free(p->t); free(p->secs); } break; }
        case 10: {
            XyD* p = (XyD*)d->ext;
            if (p) {
                for (int i = 0; i < p->nSer; i++) free(p->ser[i].vals);
                free(p->cats);
            }
            break;
        }
        case 11: { MnD* p = (MnD*)d->ext; if (p) free(p->ns); break; }
        case 12: { GitD* p = (GitD*)d->ext; if (p) { free(p->cs); free(p->bs); } break; }
    }
    free(d->ext);
    d->ext = NULL;
}

static void ExtLayout(MermaidDiagram* d, HDC hdc, const MdFonts* f) {
    switch (d->kind) {
        case 5: PieLayout(d, hdc, f); break;
        case 6: QuadLayout(d, hdc, f); break;
        case 7: TlLayout(d, hdc, f); break;
        case 8: JrnLayout(d, hdc, f); break;
        case 9: GanLayout(d, hdc, f); break;
        case 10: XyLayout(d, hdc, f); break;
        case 11: MnLayout(d, hdc, f); break;
        case 12: GitLayout(d, hdc, f); break;
    }
}

static void ExtDraw(MermaidDiagram* d, HDC hdc, int ox, int oy,
                    const MdFonts* f, const MdTheme* th) {
    switch (d->kind) {
        case 5: PieDraw(d, hdc, ox, oy, f, th); break;
        case 6: QuadDraw(d, hdc, ox, oy, f, th); break;
        case 7: TlDraw(d, hdc, ox, oy, f, th); break;
        case 8: JrnDraw(d, hdc, ox, oy, f, th); break;
        case 9: GanDraw(d, hdc, ox, oy, f, th); break;
        case 10: XyDraw(d, hdc, ox, oy, f, th); break;
        case 11: MnDraw(d, hdc, ox, oy, f, th); break;
        case 12: GitDraw(d, hdc, ox, oy, f, th); break;
    }
}
