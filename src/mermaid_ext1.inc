
static void ClassErFixSizeReal(MermaidDiagram* d, HDC hdc, const MdFonts* f);

static COLORREF MerPal(const MdTheme* th, int i) {
    static const COLORREF kL[8] = {
        RGB(86,156,214), RGB(78,201,176), RGB(220,152,95), RGB(196,121,221),
        RGB(108,179,104), RGB(224,108,117), RGB(218,166,79), RGB(120,160,190) };
    static const COLORREF kD[8] = {
        RGB(87,153,216), RGB(84,178,159), RGB(214,143,103), RGB(178,127,201),
        RGB(126,166,110), RGB(212,112,118), RGB(206,157,88), RGB(124,148,168) };
    int dark = (GetRValue(th->bg) + GetGValue(th->bg) + GetBValue(th->bg)) < 384;
    return (dark ? kD : kL)[i & 7];
}

typedef struct { wchar_t label[128]; double val; } MerPieSlice;
typedef struct { wchar_t title[160]; MerPieSlice* s; int n; double total; } PieD;

static BOOL PieParse(PieD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"title ", 6) == 0) {
            wcsncpy(p->title, TrimW(s + 6), 159);
            continue;
        }
        wchar_t* colon = wcschr(s, L':');
        if (!colon) continue;
        *colon = 0;
        wchar_t* lab = TrimW(s);
        if (lab[0] == L'"') {
            lab++;
            wchar_t* q = wcschr(lab, L'"');
            if (q) *q = 0;
        }
        double v = _wtof(TrimW(colon + 1));
        MerPieSlice* ns2 = (MerPieSlice*)realloc(p->s, ((size_t)p->n + 1) * sizeof(MerPieSlice));
        if (!ns2) return FALSE;
        p->s = ns2;
        wcsncpy(p->s[p->n].label, lab, 127);
        p->s[p->n].label[127] = 0;
        p->s[p->n].val = v;
        p->total += v;
        p->n++;
    }
    return p->n > 0 && p->total > 0;
}

typedef struct { wchar_t name[64]; double x, y; } QuadPt;
typedef struct {
    wchar_t title[160];
    wchar_t xlo[64], xhi[64], ylo[64], yhi[64];
    wchar_t q[4][64];
    QuadPt* pts; int n;
} QuadD;

static BOOL QuadParse(QuadD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    wcscpy(p->xlo, L"Low"); wcscpy(p->xhi, L"High");
    wcscpy(p->ylo, L"Low"); wcscpy(p->yhi, L"High");
    wcscpy(p->q[0], L"Quadrant 1");
    wcscpy(p->q[1], L"Quadrant 2");
    wcscpy(p->q[2], L"Quadrant 3");
    wcscpy(p->q[3], L"Quadrant 4");
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"title ", 6) == 0) { wcsncpy(p->title, TrimW(s + 6), 159); continue; }
        if (_wcsnicmp(s, L"x-axis ", 7) == 0) {
            wchar_t* rest = TrimW(s + 7);
            wchar_t* arrow = wcsstr(rest, L"-->");
            if (arrow) {
                *arrow = 0;
                wchar_t* hi = TrimW(arrow + 3);
                wchar_t* dash2 = wcsrchr(hi, L'-');
                if (dash2 && wcsstr(hi, L"- ")) { }
                wchar_t* sp = wcschr(hi, L' ');
                if (sp) *sp = 0;
                wcsncpy(p->xlo, TrimW(rest), 63);
                wcsncpy(p->xhi, hi, 63);
            } else wcsncpy(p->xhi, rest, 63);
            continue;
        }
        if (_wcsnicmp(s, L"y-axis ", 7) == 0) {
            wchar_t* rest = TrimW(s + 7);
            wchar_t* arrow = wcsstr(rest, L"-->");
            if (arrow) {
                *arrow = 0;
                wchar_t* hi = TrimW(arrow + 3);
                wchar_t* sp = wcschr(hi, L' ');
                if (sp) *sp = 0;
                wcsncpy(p->ylo, TrimW(rest), 63);
                wcsncpy(p->yhi, hi, 63);
            } else wcsncpy(p->yhi, rest, 63);
            continue;
        }
        if (_wcsnicmp(s, L"quadrant-", 9) == 0) {
            int k = s[9] - L'1';
            wchar_t* sp = wcschr(s, L' ');
            if (k >= 0 && k < 4 && sp) wcsncpy(p->q[k], TrimW(sp), 63);
            continue;
        }
        wchar_t* colon = wcschr(s, L':');
        if (colon) {
            *colon = 0;
            double x = 0, y = 0;
            if (swscanf(TrimW(colon + 1), L"[%lf, %lf]", &x, &y) == 2 ||
                swscanf(TrimW(colon + 1), L"[%lf,%lf]", &x, &y) == 2) {
                QuadPt* np = (QuadPt*)realloc(p->pts, ((size_t)p->n + 1) * sizeof(QuadPt));
                if (np) {
                    p->pts = np;
                    wcsncpy(p->pts[p->n].name, TrimW(s), 63);
                    p->pts[p->n].name[63] = 0;
                    p->pts[p->n].x = x;
                    p->pts[p->n].y = y;
                    p->n++;
                }
            }
        }
    }
    return p->n > 0;
}

typedef struct { wchar_t head[96]; wchar_t body[128]; } TlEv;
typedef struct { wchar_t title[96]; TlEv* ev; int n; } TlSec;
typedef struct { wchar_t title[160]; TlSec* secs; int n; } TlD;

static BOOL TlParse(TlD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"title ", 6) == 0) { wcsncpy(p->title, TrimW(s + 6), 159); continue; }
        if (_wcsnicmp(s, L"section ", 8) == 0) {
            TlSec* ns2 = (TlSec*)realloc(p->secs, ((size_t)p->n + 1) * sizeof(TlSec));
            if (!ns2) continue;
            p->secs = ns2;
            ZeroMemory(&p->secs[p->n], sizeof(TlSec));
            wcsncpy(p->secs[p->n].title, TrimW(s + 8), 95);
            p->n++;
            continue;
        }
        wchar_t* colon = wcschr(s, L':');
        if (!colon || p->n == 0) continue;
        *colon = 0;
        TlSec* sec = &p->secs[p->n - 1];
        TlEv* ne = (TlEv*)realloc(sec->ev, ((size_t)sec->n + 1) * sizeof(TlEv));
        if (!ne) continue;
        sec->ev = ne;
        ZeroMemory(&sec->ev[sec->n], sizeof(TlEv));
        wchar_t* head = TrimW(s);
        wchar_t* body = TrimW(colon + 1);
        wchar_t* colon2 = wcschr(body, L':');
        if (colon2) *colon2 = 0;
        wcsncpy(sec->ev[sec->n].head, head, 95);
        wcsncpy(sec->ev[sec->n].body, TrimW(body), 127);
        sec->n++;
    }
    return p->n > 0;
}

typedef struct { wchar_t task[128]; int score; wchar_t actors[256]; } JTask;
typedef struct { wchar_t name[96]; JTask* t; int n; } JSec;
typedef struct { wchar_t title[160]; JSec* secs; int n; } JrnD;

static BOOL JrnParse(JrnD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"title ", 6) == 0) { wcsncpy(p->title, TrimW(s + 6), 159); continue; }
        if (_wcsnicmp(s, L"section ", 8) == 0) {
            JSec* ns2 = (JSec*)realloc(p->secs, ((size_t)p->n + 1) * sizeof(JSec));
            if (!ns2) continue;
            p->secs = ns2;
            ZeroMemory(&p->secs[p->n], sizeof(JSec));
            wcsncpy(p->secs[p->n].name, TrimW(s + 8), 95);
            p->n++;
            continue;
        }
        wchar_t* c1 = wcschr(s, L':');
        if (!c1 || p->n == 0) continue;
        *c1 = 0;
        wchar_t* c2 = wcschr(c1 + 1, L':');
        if (!c2) continue;
        *c2 = 0;
        JSec* sec = &p->secs[p->n - 1];
        JTask* nt = (JTask*)realloc(sec->t, ((size_t)sec->n + 1) * sizeof(JTask));
        if (!nt) continue;
        sec->t = nt;
        ZeroMemory(&sec->t[sec->n], sizeof(JTask));
        wcsncpy(sec->t[sec->n].task, TrimW(s), 127);
        sec->t[sec->n].score = _wtoi(TrimW(c1 + 1));
        wcsncpy(sec->t[sec->n].actors, TrimW(c2 + 1), 255);
        sec->n++;
    }
    return p->n > 0;
}

typedef struct { wchar_t name[96]; int done, active, crit;
                 long s, e; wchar_t id[32]; int afterIdx; wchar_t after[32]; } GanTask;
typedef struct { wchar_t name[96]; } GanSec;
typedef struct { wchar_t title[160]; GanTask* t; int n;
                 GanSec* secs; int nsecs; long minD, maxD; } GanD;

static long DateToDays(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void DaysToDate(long z, int* y, int* m, int* d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    if (*m <= 2) yy++;
    *y = (int)yy;
}
static BOOL ParseYmd(const wchar_t* s, long* out) {
    int y, m, d;
    if (swscanf(s, L"%d-%d-%d", &y, &m, &d) != 3) return FALSE;
    if (y < 1900 || y > 2999 || m < 1 || m > 12 || d < 1 || d > 31) return FALSE;
    *out = DateToDays(y, m, d);
    return TRUE;
}
static BOOL ParseDur(const wchar_t* s, long* out) {
    wchar_t* end;
    double v = wcstod(s, &end);
    if (v <= 0) return FALSE;
    wchar_t u = s[wcslen(s) - 1] | 0x20;
    if (u == L'w') v *= 7;
    else if (u == L'm') v *= 30;
    else if (u == L'h') v = v / 24.0 + 1;
    *out = (long)(v + 0.5);
    return TRUE;
}

static BOOL GanParse(GanD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    p->minD = 0x7FFFFFFF; p->maxD = -0x7FFFFFFF;
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"title ", 6) == 0) { wcsncpy(p->title, TrimW(s + 6), 159); continue; }
        if (StartsWithWord(s, L"dateFormat") || StartsWithWord(s, L"axisFormat") ||
            StartsWithWord(s, L"excludes") || StartsWithWord(s, L"todayMarker") ||
            StartsWithWord(s, L"includes")) continue;
        if (StartsWithWord(s, L"section")) {
            GanSec* ns2 = (GanSec*)realloc(p->secs, ((size_t)p->nsecs + 1) * sizeof(GanSec));
            if (!ns2) continue;
            p->secs = ns2;
            wcsncpy(p->secs[p->nsecs].name, TrimW(s + 7), 95);
            p->nsecs++;
            continue;
        }
        wchar_t* colon = wcschr(s, L':');
        if (!colon) continue;
        *colon = 0;
        wchar_t toks[6][64];
        int nT = 0;
        wchar_t* q = TrimW(colon + 1);
        while (*q && nT < 6) {
            wchar_t* c = wcschr(q, L',');
            if (c) *c = 0;
            while (*q == L' ') q++;
            wchar_t* e2 = q + wcslen(q);
            while (e2 > q && e2[-1] == L' ') *--e2 = 0;
            wcsncpy(toks[nT], q, 63);
            toks[nT][63] = 0;
            nT++;
            if (!c) break;
            q = c + 1;
        }
        int ti = 0;
        GanTask t;
        ZeroMemory(&t, sizeof(t));
        wcsncpy(t.name, TrimW(s), 95);
        while (ti < nT && (_wcsicmp(toks[ti], L"done") == 0 || _wcsicmp(toks[ti], L"active") == 0 ||
                           _wcsicmp(toks[ti], L"crit") == 0 || _wcsicmp(toks[ti], L"milestone") == 0)) {
            if (_wcsicmp(toks[ti], L"done") == 0) t.done = 1;
            if (_wcsicmp(toks[ti], L"active") == 0) t.active = 1;
            if (_wcsicmp(toks[ti], L"crit") == 0) t.crit = 1;
            ti++;
        }
        if (ti < nT) { wcsncpy(t.id, toks[ti++], 31); }
        long v1 = 0, v2 = 0;
        BOOL ok = FALSE;
        if (ti < nT && _wcsnicmp(toks[ti], L"after", 5) == 0 && ti + 1 < nT) {
            wcsncpy(t.after, TrimW(toks[ti] + 5), 31);
            while (*t.after == L' ') memmove(t.after, t.after + 1, wcslen(t.after) * 2);
            ti++;
            if (ti < nT) { if (ParseDur(toks[ti], &v2)) { t.afterIdx = -2; ok = TRUE; } }
        } else if (ti + 1 < nT && ParseYmd(toks[ti], &v1)) {
            t.s = v1;
            if (ParseDur(toks[ti + 1], &v2)) { t.e = v1 + v2; ok = TRUE; }
            else if (ParseYmd(toks[ti + 1], &v2)) { t.e = v2; ok = TRUE; }
        } else if (ti + 2 < nT && _wcsnicmp(toks[ti], L"after", 5) == 0) {
            wcsncpy(t.after, TrimW(toks[ti] + 5), 31);
            ti++;
            if (ParseYmd(toks[ti], &v1) && ParseDur(toks[ti + 1], &v2)) {
                t.s = v1; t.e = v1 + v2; ok = TRUE;
            }
        }
        if (!ok) continue;
        GanTask* nt = (GanTask*)realloc(p->t, ((size_t)p->n + 1) * sizeof(GanTask));
        if (!nt) continue;
        p->t = nt;
        t.afterIdx = t.after[0] ? -2 : -1;
        p->t[p->n++] = t;
    }
    for (int i = 0; i < p->n; i++) {
        if (p->t[i].after[0]) {
            for (int j = 0; j < p->n; j++) {
                if (_wcsicmp(p->t[j].id, p->t[i].after) == 0) {
                    long dur = p->t[i].e - p->t[i].s;
                    p->t[i].s = p->t[j].e;
                    p->t[i].e = p->t[i].s + (dur > 0 ? dur : 1);
                    break;
                }
            }
            p->t[i].after[0] = 0;
        }
    }
    for (int i = 0; i < p->n; i++) {
        if (p->t[i].s < p->minD) p->minD = p->t[i].s;
        if (p->t[i].e > p->maxD) p->maxD = p->t[i].e;
    }
    return p->n > 0 && p->minD <= p->maxD;
}

typedef struct { double* vals; int n; int isBar; } XYSeries;
typedef struct { wchar_t title[160];
                 wchar_t (*cats)[64]; int nCats;
                 double yMin, yMax; int yAuto;
                 XYSeries ser[8]; int nSer; } XyD;

static double* ParseNumList(const wchar_t* s, int* nOut) {
    double* v = NULL; int n = 0;
    const wchar_t* p = s;
    while (*p) {
        while (*p == L' ' || *p == L',' || *p == L'[' || *p == L']') p++;
        if (!*p) break;
        wchar_t* end;
        double d = wcstod(p, &end);
        if (end == p) break;
        double* nv = (double*)realloc(v, ((size_t)n + 1) * sizeof(double));
        if (!nv) break;
        v = nv;
        v[n++] = d;
        p = end;
    }
    *nOut = n;
    return v;
}

static BOOL XyParse(XyD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    p->yAuto = 1;
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (_wcsnicmp(s, L"title ", 6) == 0) {
            wchar_t* t = TrimW(s + 6);
            if (*t == L'"') { t++; wchar_t* q = wcschr(t, L'"'); if (q) *q = 0; }
            wcsncpy(p->title, t, 159);
            continue;
        }
        if (_wcsnicmp(s, L"x-axis", 6) == 0) {
            wchar_t* rest = TrimW(s + 6);
            wchar_t* lb = wcschr(rest, L'[');
            wchar_t* rb = lb ? wcschr(lb, L']') : NULL;
            if (lb && rb) {
                *rb = 0;
                wchar_t* q = lb + 1;
                while (*q) {
                    wchar_t* c = wcschr(q, L',');
                    if (c) *c = 0;
                    wchar_t* item = TrimW(q);
                    if (*item == L'"') { item++; wchar_t* e3 = wcschr(item, L'"'); if (e3) *e3 = 0; }
                    if (*item) {
                        p->cats = (wchar_t(*)[64])realloc(p->cats, ((size_t)p->nCats + 1) * 64 * sizeof(wchar_t));
                        if (p->cats) { wcsncpy(p->cats[p->nCats], item, 63); p->nCats++; }
                    }
                    if (!c) break;
                    q = c + 1;
                }
            }
            continue;
        }
        if (_wcsnicmp(s, L"y-axis", 6) == 0) {
            wchar_t* arrow = wcsstr(s, L"-->");
            if (arrow) {
                *arrow = 0;
                wchar_t* b = TrimW(arrow + 3);
                wchar_t* sp = wcschr(b, L' ');
                if (sp) *sp = 0;
                p->yMin = _wtof(TrimW(wcschr(s, L' ') ? wcschr(s, L' ') + 1 : s));
                p->yMax = _wtof(b);
                p->yAuto = 0;
            }
            continue;
        }
        if (_wcsnicmp(s, L"bar", 3) == 0) {
            if (p->nSer < 8) {
                p->ser[p->nSer].vals = ParseNumList(s + 3, &p->ser[p->nSer].n);
                p->ser[p->nSer].isBar = 1;
                if (p->ser[p->nSer].n) p->nSer++;
            }
            continue;
        }
        if (_wcsnicmp(s, L"line", 4) == 0) {
            if (p->nSer < 8) {
                p->ser[p->nSer].vals = ParseNumList(s + 4, &p->ser[p->nSer].n);
                p->ser[p->nSer].isBar = 0;
                if (p->ser[p->nSer].n) p->nSer++;
            }
            continue;
        }
    }
    if (p->yAuto) {
        double mn = 0, mx = 0;
        for (int i = 0; i < p->nSer; i++)
            for (int j = 0; j < p->ser[i].n; j++) {
                if (p->ser[i].vals[j] < mn) mn = p->ser[i].vals[j];
                if (p->ser[i].vals[j] > mx) mx = p->ser[i].vals[j];
            }
        if (mx == mn) mx = mn + 1;
        p->yMin = mn; p->yMax = mx;
    }
    return p->nSer > 0;
}

typedef struct { int depth; wchar_t text[128]; int shape; int parent;
                 int x, y, w, h; } MnNode;
typedef struct { MnNode* ns; int n; } MnD;

static BOOL MnParse(MnD* p, const char* src, int len, int pos) {
    ZeroMemory(p, sizeof(*p));
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        int sp = 0;
        while (line[sp] == L' ') sp++;
        int tabs = 0;
        const wchar_t* t = line;
        while (*t == L'\t') { tabs++; t++; }
        wchar_t* s = TrimW((wchar_t*)t);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (wcsncmp(s, L"::icon", 6) == 0) continue;
        int depth = (sp + tabs * 2) / 2;
        int shape = MER_RECT;
        wchar_t text[128];
        wcsncpy(text, s, 127); text[127] = 0;
        static const struct { const wchar_t* open; const wchar_t* close; int sh; } mk[] = {
            { L"((", L"))", MER_CIRCLE },
            { L"(",  L")",  MER_STADIUM },
            { L"[",  L"]",  MER_RECT },
        };
        for (int k = 0; k < 3; k++) {
            const wchar_t* o = wcsstr(s, mk[k].open);
            if (!o) continue;
            const wchar_t* cl = wcsstr(o + wcslen(mk[k].open), mk[k].close);
            if (!cl) continue;
            int n2 = (int)(cl - (o + wcslen(mk[k].open)));
            if (n2 > 127) n2 = 127;
            wcsncpy(text, o + wcslen(mk[k].open), n2);
            text[n2] = 0;
            shape = mk[k].sh;
            break;
        }
        MnNode* nn = (MnNode*)realloc(p->ns, ((size_t)p->n + 1) * sizeof(MnNode));
        if (!nn) continue;
        p->ns = nn;
        MnNode* nd = &p->ns[p->n];
        ZeroMemory(nd, sizeof(*nd));
        nd->depth = depth;
        nd->shape = shape;
        wcscpy(nd->text, text);
        nd->parent = -1;
        p->n++;
    }
    if (p->n == 0) return FALSE;
    for (int i = 1; i < p->n; i++) {
        for (int j = i - 1; j >= 0; j--) {
            if (p->ns[j].depth < p->ns[i].depth) { p->ns[i].parent = j; break; }
        }
    }
    return p->n > 1 || p->n == 1;
}

typedef struct { int lane; int seq; wchar_t id[48]; int mergeFrom; } GitCommit;
typedef struct { wchar_t name[48]; int lane; int firstSeq; } GitBranch;
typedef struct { GitCommit* cs; int n; GitBranch* bs; int nb;
                 int cur; int nSeq; } GitD;

static int GitLaneOf(GitD* g, const wchar_t* name) {
    for (int i = 0; i < g->nb; i++)
        if (_wcsicmp(g->bs[i].name, name) == 0) return g->bs[i].lane;
    return -1;
}

static void GitAddCommit(GitD* g, const wchar_t* id, int mergeFrom) {
    GitCommit* nc = (GitCommit*)realloc(g->cs, ((size_t)g->n + 1) * sizeof(GitCommit));
    if (!nc) return;
    g->cs = nc;
    ZeroMemory(&g->cs[g->n], sizeof(GitCommit));
    g->cs[g->n].lane = g->cur;
    g->cs[g->n].seq = g->nSeq++;
    if (id && *id) wcsncpy(g->cs[g->n].id, id, 47);
    else _snwprintf(g->cs[g->n].id, 47, L"%d", g->n);
    g->cs[g->n].mergeFrom = mergeFrom;
    g->n++;
}

static BOOL GitParse(GitD* g, const char* src, int len, int pos) {
    ZeroMemory(g, sizeof(*g));
    g->bs = (GitBranch*)malloc(sizeof(GitBranch));
    if (!g->bs) return FALSE;
    wcscpy(g->bs[0].name, L"main");
    g->bs[0].lane = 0; g->bs[0].firstSeq = 0;
    g->nb = 1; g->cur = 0;
    wchar_t line[1024];
    while (pos < len) {
        if (!LineToWide(src, len, &pos, line, 1024)) continue;
        wchar_t* s = TrimW(line);
        if (!*s || wcsncmp(s, L"%%", 2) == 0) continue;
        if (StartsWithWord(s, L"commit")) {
            wchar_t idv[48] = L"";
            wchar_t* kw = wcsstr(s, L"id:");
            if (kw) {
                wchar_t* v = TrimW(kw + 3);
                if (*v == L'"') {
                    v++;
                    wchar_t* q = wcschr(v, L'"');
                    if (q) *q = 0;
                }
                wchar_t* sp = wcschr(v, L' ');
                if (sp) *sp = 0;
                wcsncpy(idv, v, 47);
            }
            GitAddCommit(g, idv, -1);
            continue;
        }
        if (StartsWithWord(s, L"branch")) {
            wchar_t* name = TrimW(s + 6);
            wchar_t* kw = wcsstr(name, L"from:");
            if (kw) *kw = 0;
            wchar_t* sp = wcschr(name, L' ');
            if (sp) *sp = 0;
            if (!*name) continue;
            GitBranch* nb2 = (GitBranch*)realloc(g->bs, ((size_t)g->nb + 1) * sizeof(GitBranch));
            if (!nb2) continue;
            g->bs = nb2;
            wcscpy(g->bs[g->nb].name, name);
            g->bs[g->nb].lane = g->nb;
            g->bs[g->nb].firstSeq = g->nSeq;
            g->nb++;
            g->cur = g->nb - 1;
            continue;
        }
        if (StartsWithWord(s, L"checkout")) {
            int ln = GitLaneOf(g, TrimW(s + 8));
            if (ln >= 0) g->cur = ln;
            continue;
        }
        if (StartsWithWord(s, L"merge")) {
            wchar_t name[48];
            wchar_t* rest = TrimW(s + 5);
            wchar_t* sp = wcschr(rest, L' ');
            if (sp) *sp = 0;
            wcsncpy(name, rest, 47);
            name[47] = 0;
            int ln = GitLaneOf(g, name);
            GitAddCommit(g, L"", ln);
            continue;
        }
    }
    return g->n > 0;
}
