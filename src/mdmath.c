
#include "common.h"
#include <stdio.h>
#include <stdlib.h>

static int MbFontH(HDC hdc, HFONT f) {
    HFONT of = (HFONT)SelectObject(hdc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, of);
    return tm.tmHeight;
}
#define FontH(hdc, f) MbFontH(hdc, f)

static MathBox* MbNew(MbKind k) {
    MathBox* b = (MathBox*)calloc(1, sizeof(MathBox));
    if (b) b->kind = k;
    return b;
}

void Math_Free(MathBox* b) {
    if (!b) return;
    for (int i = 0; i < b->nKids; i++) Math_Free(b->kids[i]);
    free(b->kids);
    free(b->text);
    free(b);
}

static void MbAdd(MathBox* row, MathBox* kid) {
    if (!row) return;
    MathBox** nk = (MathBox**)realloc(row->kids,
        ((size_t)row->nKids + 1) * sizeof(MathBox*));
    if (!nk) return;
    row->kids = nk;
    row->kids[row->nKids++] = kid;
}

typedef struct { const wchar_t* p; const wchar_t* end; } MScan;

static const struct { const wchar_t* cmd; wchar_t ch; } kSym[] = {
    { L"alpha", 0x3B1 }, { L"beta", 0x3B2 }, { L"gamma", 0x3B3 }, { L"delta", 0x3B4 },
    { L"epsilon", 0x3F5 }, { L"varepsilon", 0x3B5 }, { L"zeta", 0x3B6 }, { L"eta", 0x3B7 },
    { L"theta", 0x3B8 }, { L"vartheta", 0x3D1 }, { L"iota", 0x3B9 }, { L"kappa", 0x3BA },
    { L"lambda", 0x3BB }, { L"mu", 0x3BC }, { L"nu", 0x3BD }, { L"xi", 0x3BE },
    { L"pi", 0x3C0 }, { L"rho", 0x3C1 }, { L"sigma", 0x3C3 }, { L"tau", 0x3C4 },
    { L"upsilon", 0x3C5 }, { L"phi", 0x3D5 }, { L"varphi", 0x3C6 }, { L"chi", 0x3C7 },
    { L"psi", 0x3C8 }, { L"omega", 0x3C9 },
    { L"Gamma", 0x393 }, { L"Delta", 0x394 }, { L"Theta", 0x398 }, { L"Lambda", 0x39B },
    { L"Xi", 0x39E }, { L"Pi", 0x3A0 }, { L"Sigma", 0x3A3 }, { L"Phi", 0x3A6 },
    { L"Psi", 0x3A8 }, { L"Omega", 0x3A9 },
    { L"to", 0x2192 }, { L"rightarrow", 0x2192 }, { L"leftarrow", 0x2190 },
    { L"Rightarrow", 0x21D2 }, { L"Leftarrow", 0x21D0 }, { L"leftrightarrow", 0x2194 },
    { L"times", 0xD7 }, { L"cdot", 0x22C5 }, { L"pm", 0xB1 }, { L"mp", 0x2213 },
    { L"leq", 0x2264 }, { L"geq", 0x2265 }, { L"neq", 0x2260 }, { L"approx", 0x2248 },
    { L"equiv", 0x2261 }, { L"sim", 0x223C }, { L"propto", 0x221D },
    { L"infty", 0x221E }, { L"partial", 0x2202 }, { L"nabla", 0x2207 },
    { L"in", 0x2208 }, { L"notin", 0x2209 }, { L"ni", 0x220B },
    { L"subset", 0x2282 }, { L"subseteq", 0x2286 }, { L"supset", 0x2283 },
    { L"cup", 0x222A }, { L"cap", 0x2229 }, { L"emptyset", 0x2205 },
    { L"wedge", 0x2227 }, { L"vee", 0x2228 }, { L"neg", 0xAC }, { L"lnot", 0xAC },
    { L"forall", 0x2200 }, { L"exists", 0x2203 },
    { L"ldots", 0x2026 }, { L"cdots", 0x22EF }, { L"vdots", 0x22EE },
    { L"langle", 0x27E8 }, { L"rangle", 0x27E9 },
    { L"prime", 0x2032 }, { L"circ", 0x2218 }, { L"bullet", 0x2219 },
    { L"oplus", 0x2295 }, { L"otimes", 0x2297 },
};

static int BbAppend(wchar_t* out, wchar_t c) {
    unsigned int cp;
    switch (c) {
        case L'R': cp = 0x211D; break;
        case L'N': cp = 0x2115; break;
        case L'Z': cp = 0x2124; break;
        case L'Q': cp = 0x211A; break;
        case L'C': cp = 0x2102; break;
        case L'E': cp = 0x1D53C; break;
        case L'P': cp = 0x2119; break;
        case L'H': cp = 0x210D; break;
        case L'F': cp = 0x1D53D; break;
        default: out[0] = c; return 1;
    }
    if (cp > 0xFFFF) {
        out[0] = (wchar_t)(0xD800 + ((cp - 0x10000) >> 10));
        out[1] = (wchar_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        return 2;
    }
    out[0] = (wchar_t)cp;
    return 1;
}

static MathBox* MbGlyphs(const wchar_t* s, int len, int level, BOOL italic) {
    if (len <= 0) return NULL;
    MathBox* b = MbNew(MB_GLYPHS);
    b->text = (wchar_t*)malloc(((size_t)len + 1) * sizeof(wchar_t));
    memcpy(b->text, s, (size_t)len * sizeof(wchar_t));
    b->text[len] = 0;
    b->level = level;
    b->italic = italic;
    return b;
}

static MathBox* MathParseRow(MScan* s, int level);

/* ---- 环境扩展：\begin{...}...\end{...} ---- */

static int EnvTypeFromName(const wchar_t* e) {
    if (!e || !*e) return -1;
    if (wcscmp(e, L"cases") == 0 || wcscmp(e, L"dcases") == 0) return MBENV_CASES;
    if (wcscmp(e, L"matrix") == 0) return MBENV_PLAIN;
    if (wcscmp(e, L"pmatrix") == 0) return MBENV_PAREN;
    if (wcscmp(e, L"bmatrix") == 0) return MBENV_BRACKET;
    if (wcscmp(e, L"Bmatrix") == 0) return MBENV_BRACE;
    if (wcscmp(e, L"vmatrix") == 0) return MBENV_VBAR;
    if (wcscmp(e, L"Vmatrix") == 0) return MBENV_VVBAR;
    if (wcscmp(e, L"aligned") == 0 || wcscmp(e, L"align") == 0 ||
        wcscmp(e, L"align*") == 0 || wcscmp(e, L"gather") == 0 ||
        wcscmp(e, L"gathered") == 0) return MBENV_PLAIN;
    return -1;
}

/* 解析环境体：& 分列、\\ 分行，cell 的源码先原样积累再交给 MathParseRow
   （嵌套环境随之自然递归）。消费到与之匹配的 \end{...} 为止。 */
static MathBox* ParseEnv(MScan* s, int ev, int level) {
    MathBox* envb = MbNew(MB_ENV);
    envb->level = level;
    envb->env = ev;
    MathBox* row = MbNew(MB_ROW);
    row->level = level;
    wchar_t cell[768];
    int nc = 0;
    int depth = 0;      /* { } 嵌套 */
    int nesting = 0;    /* 嵌套 \begin/\end */
    int maxCols = 0;

    #define ENV_FLUSHCELL() do { \
        while (nc > 0 && (cell[nc - 1] == L' ' || cell[nc - 1] == L'\t')) nc--; \
        const wchar_t* cs = cell; \
        while (cs < cell + nc && (*cs == L' ' || *cs == L'\t')) cs++; \
        if (nc > 0 && cs < cell + nc) { \
            cell[nc] = 0; \
            MScan sub = { cs, cell + nc }; \
            MathBox* cb = MathParseRow(&sub, level); \
            if (cb) MbAdd(row, cb); \
            nc = 0; \
        } else nc = 0; \
    } while (0)
    #define ENV_FLUSHROW() do { \
        ENV_FLUSHCELL(); \
        if (row->nKids > 0) { \
            if (row->nKids > maxCols) maxCols = row->nKids; \
            MbAdd(envb, row); \
        } else { \
            Math_Free(row); \
        } \
        row = MbNew(MB_ROW); \
        row->level = level; \
    } while (0)

    while (s->p < s->end) {
        wchar_t c = *s->p;
        if (c == L'\\' && s->p + 1 < s->end) {
            wchar_t c2 = s->p[1];
            if (c2 == L'\\' && depth == 0 && nesting == 0) {
                s->p += 2;
                if (s->p < s->end && *s->p == L'[') {   /* \\[2pt] 行距参数 */
                    while (s->p < s->end && *s->p != L']') s->p++;
                    if (s->p < s->end) s->p++;
                }
                ENV_FLUSHROW();
                continue;
            }
            if ((c2 == L'b' && wcsncmp(s->p, L"\\begin", 6) == 0) && depth == 0) {
                /* 嵌套环境开始：连同 {name} 原样拷入 cell */
                if (nc < 760) cell[nc++] = c;
                s->p++;
                for (int k = 0; k < 5 && s->p < s->end; k++) {
                    if (nc < 760) cell[nc++] = *s->p;
                    s->p++;
                }
                if (s->p < s->end && *s->p == L'{') {
                    while (s->p < s->end && *s->p != L'}') {
                        if (nc < 760) cell[nc++] = *s->p;
                        s->p++;
                    }
                    if (s->p < s->end) { if (nc < 760) cell[nc++] = *s->p; s->p++; }
                }
                nesting++;
                continue;
            }
            if (c2 == L'e' && wcsncmp(s->p, L"\\end", 4) == 0 && depth == 0) {
                if (nesting > 0) {   /* 嵌套环境的 \end：原样拷入 */
                    if (nc < 760) cell[nc++] = c;
                    s->p++;
                    for (int k = 0; k < 3 && s->p < s->end; k++) {
                        if (nc < 760) cell[nc++] = *s->p;
                        s->p++;
                    }
                    if (s->p < s->end && *s->p == L'{') {
                        while (s->p < s->end && *s->p != L'}') {
                            if (nc < 760) cell[nc++] = *s->p;
                            s->p++;
                        }
                        if (s->p < s->end) { if (nc < 760) cell[nc++] = *s->p; s->p++; }
                    }
                    nesting--;
                    continue;
                }
                /* 当前环境的 \end：消费并结束 */
                s->p += 4;
                if (s->p < s->end && *s->p == L'{') {
                    while (s->p < s->end && *s->p != L'}') s->p++;
                    if (s->p < s->end) s->p++;
                }
                break;
            }
            /* 其它命令：反斜杠 + 后一字符 + 字母尾巴整体拷入，防止 \frac 断开 */
            if (nc < 762) { cell[nc++] = c; cell[nc++] = c2; }
            s->p += 2;
            while (s->p < s->end && iswalpha(*s->p) && nc < 766) cell[nc++] = *s->p++;
            continue;
        }
        if (c == L'&' && depth == 0 && nesting == 0) {
            s->p++;
            ENV_FLUSHCELL();
            continue;
        }
        if (c == L'{') depth++;
        if (c == L'}' && depth > 0) depth--;
        if (nc < 766) cell[nc++] = c;
        s->p++;
    }
    ENV_FLUSHROW();
    #undef ENV_FLUSHCELL
    #undef ENV_FLUSHROW

    if (envb->nKids == 0 || maxCols == 0) {
        Math_Free(envb);
        return NULL;
    }
    envb->cols = maxCols;
    return envb;
}

static MathBox* MathParseGroup(MScan* s, int level) {
    if (s->p < s->end && *s->p == L'{') {
        s->p++;
        MathBox* row = MathParseRow(s, level);
        if (s->p < s->end && *s->p == L'}') s->p++;
        return row;
    }
    const wchar_t* q = s->p;
    if (q < s->end && *q == L'\\' && q + 1 < s->end && iswalpha(q[1])) {
        q++;
        while (q < s->end && iswalpha(*q)) q++;
    } else if (q < s->end) {
        q++;
    }
    if (q == s->p) return NULL;
    MScan sub = { s->p, q };
    MathBox* b = MathParseRow(&sub, level);
    s->p = q;
    return b;
}

static MathBox* MathParseRow(MScan* s, int level) {
    MathBox* row = MbNew(MB_ROW);
    row->level = level;
    wchar_t buf[256];
    int nb = 0;
    int lastLevel = level;
    BOOL lastItalic = TRUE;

    #define FLUSH() do { \
        if (nb > 0) { MbAdd(row, MbGlyphs(buf, nb, lastLevel, lastItalic)); nb = 0; } \
    } while (0)

    while (s->p < s->end) {
        wchar_t c = *s->p;
        if (getenv("MDTRACE")) fprintf(stderr, "[PR] lvl=%d c='%lc' nb=%d nKids=%d\n", level, c, nb, row->nKids);
        if (c == L'}') break;
        if (c == L'{') {
            FLUSH();
            MathBox* g = MathParseGroup(s, level);
            if (g) MbAdd(row, g);
            continue;
        }
        if (c == L'^' || c == L'_') {
            FLUSH();
            s->p++;
            if (getenv("MDTRACE")) fprintf(stderr, "[PR] script enter\n");
            MathBox* sc = MathParseGroup(s, level < 2 ? level + 1 : 2);
            MathBox* target = NULL;
            if (row->nKids > 0 && row->kids[row->nKids - 1]->kind == MB_SCRIPT)
                target = row->kids[row->nKids - 1];
            if (!target) {
                target = MbNew(MB_SCRIPT);
                target->level = level;
                MbAdd(row, target);
            }
            if (c == L'^') {
                if (target->nKids >= 2 && target->kids[1]) {  Math_Free(target->kids[1]); target->kids[1] = sc; }
                else { while (target->nKids < 2) MbAdd(target, NULL); target->kids[1] = sc; }
            } else {
                if (target->nKids >= 1 && target->kids[0] && target->kids[0]->kind != MB_GLYPHS) {
                    while (target->nKids < 3) MbAdd(target, NULL);
                    if (target->kids[2]) Math_Free(target->kids[2]);
                    target->kids[2] = sc;
                } else if (target->nKids == 0) {
                    MbAdd(target, NULL);
                    while (target->nKids < 3) MbAdd(target, NULL);
                    target->kids[2] = sc;
                } else {
                    while (target->nKids < 3) MbAdd(target, NULL);
                    if (target->kids[2]) Math_Free(target->kids[2]);
                    target->kids[2] = sc;
                }
            }
            continue;
        }
        if (c == L'\\') {
            s->p++;
            if (s->p >= s->end) break;
            wchar_t c2 = *s->p;
            if (c2 == L'\\' || c2 == L';' || c2 == L',' || c2 == L' ' ||
                c2 == L'!' || c2 == L':') {
                if (c2 == L'\\') { FLUSH(); MathBox* sp = MbGlyphs(L"  ", 2, level, FALSE); sp->spaceAfter = UI_Scale(8); MbAdd(row, sp); }
                else if (c2 == L',' || c2 == L';') { FLUSH(); MathBox* sp = MbGlyphs(L" ", 1, level, FALSE); MbAdd(row, sp); }
                else if (c2 == L' ') { FLUSH(); MathBox* sp = MbGlyphs(L" ", 1, level, FALSE); MbAdd(row, sp); }
                s->p++;
                continue;
            }
            if (c2 == L'{' || c2 == L'}' || c2 == L'|' || c2 == L'%' ||
                c2 == L'&' || c2 == L'#' || c2 == L'$' || c2 == L'_') {
                if (nb < 255) buf[nb++] = c2;
                s->p++;
                continue;
            }
            wchar_t cmd[24];
            int nc = 0;
            while (s->p < s->end && ((iswalpha(*s->p) && nc < 22) || nc == 0)) {
                cmd[nc++] = *s->p++;
                if (!iswalpha(*s->p)) break;
            }
            cmd[nc] = 0;
            if (nc == 0) continue;
            if (wcscmp(cmd, L"frac") == 0 || wcscmp(cmd, L"dfrac") == 0 ||
                wcscmp(cmd, L"tfrac") == 0) {
                FLUSH();
                MathBox* f = MbNew(MB_FRAC);
                f->level = level;
                MathBox* a = MathParseGroup(s, level);
                MathBox* b = MathParseGroup(s, level);
                if (a) MbAdd(f, a);
                if (b) MbAdd(f, b);
                MbAdd(row, f);
                continue;
            }
            if (wcscmp(cmd, L"sqrt") == 0) {
                FLUSH();
                if (s->p < s->end && *s->p == L'[') {
                    while (s->p < s->end && *s->p != L']') s->p++;
                    if (s->p < s->end) s->p++;
                }
                MathBox* r = MbNew(MB_RADICAL);
                r->level = level;
                MathBox* b = MathParseGroup(s, level);
                if (b) MbAdd(r, b);
                MbAdd(row, r);
                continue;
            }
            if (wcscmp(cmd, L"sum") == 0 || wcscmp(cmd, L"prod") == 0 ||
                wcscmp(cmd, L"int") == 0 || wcscmp(cmd, L"iint") == 0 ||
                wcscmp(cmd, L"bigcup") == 0 || wcscmp(cmd, L"bigcap") == 0 ||
                wcscmp(cmd, L"lim") == 0 || wcscmp(cmd, L"max") == 0 ||
                wcscmp(cmd, L"min") == 0 || wcscmp(cmd, L"log") == 0 ||
                wcscmp(cmd, L"ln") == 0 || wcscmp(cmd, L"sin") == 0 ||
                wcscmp(cmd, L"cos") == 0 || wcscmp(cmd, L"tan") == 0 ||
                wcscmp(cmd, L"exp") == 0) {
                FLUSH();
                const wchar_t* glyph;
                switch (cmd[0]) {
                    case L's': glyph = L"\x2211"; break;
                    case L'p': glyph = L"\x220F"; break;
                    case L'i':
                        if (cmd[1] == L'i') glyph = L"\x222C";
                        else glyph = L"\x222B";
                        break;
                    case L'b': glyph = (cmd[3] == L'u') ? L"\x22C3" : L"\x22C2"; break;
                    default: glyph = cmd; break;
                }
                MathBox* op = MbNew(MB_BIGOP);
                op->level = level;
                wchar_t tmp[8];
                int gl = (int)wcslen(glyph);
                if (gl > 7) gl = 7;
                memcpy(tmp, glyph, (size_t)gl * sizeof(wchar_t));
                tmp[gl] = 0;
                BOOL textOp = (gl > 1 && glyph[0] >= 0x80 ? FALSE : gl > 1);
                if (cmd[0] == L'l' && wcscmp(cmd, L"lim") == 0) textOp = TRUE;
                if (wcschr(L"lms", cmd[0]) && wcscmp(cmd, L"ln") != 0 && gl == wcslen(cmd) &&
                    (wcscmp(cmd, L"lim")==0 || wcscmp(cmd, L"log")==0 || wcscmp(cmd, L"ln")==0 ||
                     wcscmp(cmd, L"sin")==0 || wcscmp(cmd, L"cos")==0 || wcscmp(cmd, L"tan")==0 ||
                     wcscmp(cmd, L"max")==0 || wcscmp(cmd, L"min")==0 || wcscmp(cmd, L"exp")==0))
                    textOp = TRUE;
                {
                    wchar_t g2[8];
                    int n2 = 0;
                    for (const wchar_t* q = tmp; *q && n2 < 7; q++) g2[n2++] = *q;
                    g2[n2] = 0;
                    MathBox* g = MbGlyphs(g2, n2, level, !textOp);
                    if (g) MbAdd(op, g);
                }
                MbAdd(row, op);
                continue;
            }
            if (wcscmp(cmd, L"text") == 0 || wcscmp(cmd, L"mathrm") == 0 ||
                wcscmp(cmd, L"mbox") == 0) {
                FLUSH();
                if (s->p < s->end && *s->p == L'{') {
                    s->p++;
                    wchar_t tbuf[128];
                    int nt = 0;
                    while (s->p < s->end && *s->p != L'}' && nt < 127)
                        tbuf[nt++] = *s->p++;
                    if (s->p < s->end) s->p++;
                    if (nt > 0) MbAdd(row, MbGlyphs(tbuf, nt, level, FALSE));
                }
                continue;
            }
            if (wcscmp(cmd, L"mathbf") == 0) {
                FLUSH();
                MathBox* g = MathParseGroup(s, level);
                if (g) MbAdd(row, g);
                continue;
            }
            if (wcscmp(cmd, L"mathbb") == 0) {
                FLUSH();
                if (s->p < s->end && *s->p == L'{') {
                    s->p++;
                    wchar_t tbuf[64];
                    int nt = 0;
                    while (s->p < s->end && *s->p != L'}' && nt < 62)
                        nt += BbAppend(tbuf + nt, *s->p++);
                    if (s->p < s->end) s->p++;
                    if (nt > 0) MbAdd(row, MbGlyphs(tbuf, nt, level, FALSE));
                } else if (s->p < s->end) {
                    wchar_t one[3] = { 0, 0, 0 };
                    int n = BbAppend(one, *s->p++);
                    MbAdd(row, MbGlyphs(one, n, level, FALSE));
                }
                continue;
            }
            if (wcscmp(cmd, L"left") == 0 || wcscmp(cmd, L"right") == 0) {
                if (s->p < s->end && *s->p == L'.') s->p++;
                continue;
            }
            if (wcscmp(cmd, L"begin") == 0) {
                wchar_t env[24];
                int ne = 0;
                if (s->p < s->end && *s->p == L'{') {
                    s->p++;
                    while (s->p < s->end && *s->p != L'}' && ne < 22) env[ne++] = *s->p++;
                    if (s->p < s->end) s->p++;
                }
                env[ne] = 0;
                int ev = EnvTypeFromName(env);
                if (ev >= 0) {
                    FLUSH();
                    MathBox* envb = ParseEnv(s, ev, level);
                    if (envb) MbAdd(row, envb);
                    continue;
                }
                /* 未知环境：吞掉 {name}，内容按普通序列解析（沿用旧行为） */
                continue;
            }
            if (wcscmp(cmd, L"end") == 0) {
                if (s->p < s->end && *s->p == L'{') {
                    while (s->p < s->end && *s->p != L'}') s->p++;
                    if (s->p < s->end) s->p++;
                }
                continue;
            }
            int found = -1;
            for (int k = 0; k < (int)(sizeof(kSym) / sizeof(kSym[0])); k++) {
                if (wcscmp(cmd, kSym[k].cmd) == 0) { found = k; break; }
            }
            if (found >= 0) {
                FLUSH();
                wchar_t one[2] = { kSym[found].ch, 0 };
                MathBox* g = MbGlyphs(one, 1, level, TRUE);
                if (g) { g->spaceAfter = UI_Scale(2); MbAdd(row, g); }
                continue;
            }
            FLUSH();
            wchar_t ub[24];
            ub[0] = L'\\';
            int u2 = 1;
            for (const wchar_t* q = cmd; *q && u2 < 22; q++) ub[u2++] = *q;
            MbAdd(row, MbGlyphs(ub, u2, level, FALSE));
            continue;
        }
        BOOL it = iswalpha(c) != 0;
        if (nb > 0 && (it != lastItalic || lastLevel != level)) FLUSH();
        lastItalic = it;
        lastLevel = level;
        if (nb < 255) buf[nb++] = c;
        s->p++;
    }
    FLUSH();
    #undef FLUSH
    if (row->nKids == 0) {
        Math_Free(row);
        return NULL;
    }
    if (row->nKids == 1) {
        MathBox* only = row->kids[0];
        free(row->kids);
        free(row);
        return only;
    }
    return row;
}

/* \(..\) / \[..\] 数学定界符归一为 $..$ / $$..$$（md_parse 前做，LaTeX 系
   来源的公式免改写）。逐行跟踪围栏代码块整块跳过；"\\"（换行命令）后跟
   分隔符的情形按反斜杠奇偶不转换。原地收缩重写，返回新长度。 */
void MdNormalizeMathDelims(char* s, DWORD* len) {
    DWORD n = *len;
    DWORD r = 0, i = 0;
    BOOL inFence = FALSE;
    BOOL lineOnlySpace = TRUE;
    int bs = 0;   /* 连续反斜杠计数（跨字符维护，避免读已被改写的缓冲） */
    while (i < n) {
        char c = s[i];
        if (c == '\n') {
            s[r++] = c;
            i++;
            lineOnlySpace = TRUE;
            bs = 0;
            continue;
        }
        if (lineOnlySpace && (c == '`' || c == '~')) {
            int k = 0;
            while (i + k < n && s[i + k] == c) k++;
            if (k >= 3) inFence = !inFence;
        }
        if (c == '\\') {
            if (!inFence && i + 1 < n &&
                (s[i + 1] == '(' || s[i + 1] == '[' ||
                 s[i + 1] == ')' || s[i + 1] == ']') &&
                ((bs + 1) & 1)) {
                s[r++] = '$';
                if (s[i + 1] == '[' || s[i + 1] == ']') s[r++] = '$';
                i += 2;
                bs = 0;
                continue;
            }
            bs++;
        } else {
            bs = 0;
        }
        s[r++] = c;
        i++;
        if (c != ' ' && c != '\t' && c != '\r') lineOnlySpace = FALSE;
    }
    if (r < n) s[r] = '\0';
    *len = r;
}

MathBox* Math_Build(const wchar_t* latex) {
    if (!latex || !*latex) return NULL;
    MScan s = { latex, latex + wcslen(latex) };
    return MathParseRow(&s, 0);
}

static HFONT MathFont(const MdFonts* f, int level, BOOL italic) {
    switch (level) {
        case 1:  return italic ? f->mathItS : f->mathUpS;
        case 2:  return italic ? f->mathItSS : f->mathUpSS;
        default: return italic ? f->mathIt : f->mathUp;
    }
}

/* em（字号）按 HFONT 缓存（GetObjectW 逐字形调用也是热点之一），
   随 Math_MeasureCacheReset 一并失效 */
static HFONT s_emF[16];
static int s_emV[16];

static int MathFontEm(HFONT fo) {
    for (int i = 0; i < 16; i++)
        if (s_emF[i] == fo) return s_emV[i];
    LOGFONTW lf;
    int em = 16;
    if (GetObjectW(fo, sizeof(lf), &lf) && lf.lfHeight < 0)
        em = -lf.lfHeight;
    for (int i = 0; i < 16; i++) {
        if (!s_emF[i]) { s_emF[i] = fo; s_emV[i] = em; break; }
    }
    return em;
}

static void MbMeasure(MathBox* b, HDC hdc, const MdFonts* f);

/* ---- 测量缓存 ----
   MB_GLYPHS 每个字形盒要 SelectObject×2 + GetTextExtentPoint32W + GetObjectW，
   公式密集文档首屏布局时是最大热点；数学字形的 (字体,文本) 组合高度重复，
   按 FNV 哈希缓存 宽度/em，em 另有按 HFONT 的小缓存。字体重建（字号/DPI
   变化）时由 mdview 调 Math_MeasureCacheReset 整表失效。 */
#define MG_BUCKETS 512
typedef struct MgEnt {
    struct MgEnt* next;
    HFONT f;
    wchar_t* s;
    int cx, em;
} MgEnt;
static MgEnt* s_mg[MG_BUCKETS];
static int s_mgCount = 0;

static unsigned MgHash(HFONT f, const wchar_t* s) {
    unsigned h = 2166136261u ^ (unsigned)(uintptr_t)f;
    for (const wchar_t* p = s; *p; p++) { h ^= (unsigned)*p; h *= 16777619u; }
    return h & (MG_BUCKETS - 1);
}

void Math_MeasureCacheReset(void) {
    for (int i = 0; i < MG_BUCKETS; i++) {
        MgEnt* e = s_mg[i];
        while (e) {
            MgEnt* nx = e->next;
            free(e->s);
            free(e);
            e = nx;
        }
        s_mg[i] = NULL;
    }
    s_mgCount = 0;
    for (int i = 0; i < 16; i++) { s_emF[i] = NULL; s_emV[i] = 0; }   /* em 缓存一并失效 */
}

static BOOL MgGet(HFONT f, const wchar_t* s, int* cx, int* em) {
    for (MgEnt* e = s_mg[MgHash(f, s)]; e; e = e->next) {
        if (e->f == f && wcscmp(e->s, s) == 0) {
            *cx = e->cx;
            *em = e->em;
            return TRUE;
        }
    }
    return FALSE;
}

static void MgPut(HFONT f, const wchar_t* s, int cx, int em) {
    if (s_mgCount > 4096) return;   /* 上限保护 */
    MgEnt* e = (MgEnt*)malloc(sizeof(MgEnt));
    if (!e) return;
    e->s = _wcsdup(s);
    if (!e->s) { free(e); return; }
    e->f = f;
    e->cx = cx;
    e->em = em;
    unsigned b = MgHash(f, s);
    e->next = s_mg[b];
    s_mg[b] = e;
    s_mgCount++;
}


static void MbMeasureKids(MathBox* b, HDC hdc, const MdFonts* f) {
    for (int i = 0; i < b->nKids; i++)
        if (b->kids[i]) MbMeasure(b->kids[i], hdc, f);
}

static void MbMeasure(MathBox* b, HDC hdc, const MdFonts* f) {
    switch (b->kind) {
        case MB_GLYPHS: {
            HFONT fo = MathFont(f, b->level, b->italic);
            int gcx = 0, gem = 0;
            if (!MgGet(fo, b->text, &gcx, &gem)) {
                HFONT of = (HFONT)SelectObject(hdc, fo);
                SIZE sz;
                GetTextExtentPoint32W(hdc, b->text, (int)wcslen(b->text), &sz);
                SelectObject(hdc, of);
                gem = MathFontEm(fo);
                MgPut(fo, b->text, sz.cx, gem);
                gcx = sz.cx;
            }
            b->w = gcx + b->spaceAfter;
            b->h = gem;
            b->asc = gem * 3 / 4;
            break;
        }
        case MB_ROW: {
            MbMeasureKids(b, hdc, f);
            int w = 0, h = 0, asc = 0;
            for (int i = 0; i < b->nKids; i++) {
                MathBox* k = b->kids[i];
                if (!k) continue;
                w += k->w;
                int ka = k->asc, kd = k->h - k->asc;
                if (ka > asc) asc = ka;
                if (asc + kd > h) h = asc + kd;
            }
            b->w = w;
            b->h = h;
            b->asc = asc;
            break;
        }
        case MB_FRAC: {
            MbMeasureKids(b, hdc, f);
            MathBox* num = b->nKids > 0 ? b->kids[0] : NULL;
            MathBox* den = b->nKids > 1 ? b->kids[1] : NULL;
            int nw = num ? num->w : 0, dw = den ? den->w : 0;
            int w = (nw > dw ? nw : dw) + UI_Scale(8);
            int em = MathFontEm(MathFont(f, b->level, TRUE));
            int axis = em / 4;
            int gap = UI_Scale(2);
            int nu = num ? num->asc : 0;
            int du = den ? den->asc : 0;
            b->w = w;
            b->asc = nu + gap + axis;
            if (b->asc < em * 3 / 4) b->asc = em * 3 / 4;
            b->h = b->asc + axis + gap + du;
            if (b->h < em) b->h = em;
            (void)0;
            break;
        }
        case MB_SCRIPT: {
            MbMeasureKids(b, hdc, f);
            MathBox* base = b->nKids > 0 ? b->kids[0] : NULL;
            MathBox* sup = b->nKids > 1 ? b->kids[1] : NULL;
            MathBox* sub = b->nKids > 2 ? b->kids[2] : NULL;
            int w = base ? base->w : 0;
            int baseAsc = base ? base->asc
                               : MathFontEm(MathFont(f, b->level, FALSE)) * 3 / 4;
            int asc = baseAsc, desc = base ? base->h - base->asc : 0;
            b->raiseY = baseAsc * 3 / 5;
            if (sup) {
                if (base && base->w + sup->w > w) w = base->w + sup->w;
                else if (!base) w = sup->w;
                if (b->raiseY + sup->asc > asc) asc = b->raiseY + sup->asc;
            }
            if (sub) {
                if (base && base->w + sub->w > w) w = base->w + sub->w;
                else if (!base) w = sub->w;
                desc += sub->h - sub->asc / 2;
            }
            b->w = w;
            b->h = asc + desc;
            b->asc = asc;
            break;
        }
        case MB_RADICAL: {
            MbMeasureKids(b, hdc, f);
            MathBox* body = b->nKids > 0 ? b->kids[0] : NULL;
            int bw = body ? body->w : 0;
            int bh = body ? body->h : 0;
            int ba = body ? body->asc : 0;
            int glyphW = UI_Scale(6 + b->level * 2);
            b->w = glyphW + UI_Scale(4) + bw + UI_Scale(3);
            b->h = bh + UI_Scale(4);
            b->asc = ba + UI_Scale(4);
            if (b->asc < ba + UI_Scale(4)) b->asc = ba + UI_Scale(4);
            break;
        }
        case MB_BIGOP: {
            MbMeasureKids(b, hdc, f);
            MathBox* g = b->nKids > 0 ? b->kids[0] : NULL;
            int w = g ? g->w : 0;
            int gh = g ? g->h : 0;
            int ga = g ? g->asc : 0;
            b->w = w + UI_Scale(4);
            b->h = gh;
            b->asc = ga;
            break;
        }
        case MB_ENV: {
            /* 环境网格：列宽取各列最大，行高取各行最大；测量所有 cell */
            int nRows = b->nKids;
            int nCols = b->cols > 0 ? b->cols : 1;
            if (nCols > 16) nCols = 16;
            if (nRows > 64) nRows = 64;
            int em = MathFontEm(MathFont(f, b->level, FALSE));
            int colW[16];
            int rowH[64], rowAsc[64];
            for (int j = 0; j < nCols; j++) colW[j] = 0;
            for (int i = 0; i < nRows; i++) { rowH[i] = em; rowAsc[i] = em * 3 / 4; }
            for (int i = 0; i < nRows; i++) {
                const MathBox* r = b->kids[i];
                if (!r) continue;
                for (int j = 0; j < r->nKids && j < nCols; j++) {
                    MbMeasure(r->kids[j], hdc, f);
                    if (r->kids[j]->w > colW[j]) colW[j] = r->kids[j]->w;
                    if (r->kids[j]->h > rowH[i]) rowH[i] = r->kids[j]->h;
                    if (r->kids[j]->asc > rowAsc[i]) rowAsc[i] = r->kids[j]->asc;
                }
            }
            int cg = UI_Scale(8), rg = UI_Scale(4);
            int innerW = UI_Scale(8) * (nCols - 1);
            for (int j = 0; j < nCols; j++) innerW += colW[j];
            int dw = UI_Scale(10);
            int sides = (b->env == MBENV_PLAIN) ? 0 : (b->env == MBENV_CASES ? 1 : 2);
            int totalH = rg * (nRows - 1);
            for (int i = 0; i < nRows; i++) totalH += rowH[i];
            b->w = innerW + UI_Scale(4) + dw * sides;
            b->h = totalH;
            b->asc = totalH - (rowH[nRows - 1] - rowAsc[nRows - 1]);
            break;
        }
    }
}

void Math_Measure(MathBox* b, HDC hdc, const MdFonts* f) {
    if (b) MbMeasure(b, hdc, f);
}

static void MbDraw(const MathBox* b, HDC hdc, int x, int yBase,
                   const MdFonts* f, const MdTheme* th);

static void MbDrawKids(const MathBox* b, HDC hdc, int x, int yBase,
                       const MdFonts* f, const MdTheme* th, int dx, int dy) {
    for (int i = 0; i < b->nKids; i++)
        if (b->kids[i]) MbDraw(b->kids[i], hdc, x + dx, yBase + dy, f, th);
}

/* 大分隔符（任意高度）：贝塞尔/线段绘制，随环境高度伸缩 */
static void EnvDelim(HDC hdc, int x, int yTop, int yBot, int w, int shape) {
    /* shape: 0=( 1=) 2=[ 3=] 4={ 5=} 6=| 7=‖ */
    int h = yBot - yTop;
    if (h < 2) h = 2;
    int ym = yTop + h / 2;
    int q = h / 4;
    if (q < 2) q = 2;
    POINT pts[4];
    switch (shape) {
        case 0:  /* ( */
            pts[0].x = x + w; pts[0].y = yTop;
            pts[1].x = x;     pts[1].y = yTop + q;
            pts[2].x = x;     pts[2].y = yBot - q;
            pts[3].x = x + w; pts[3].y = yBot;
            PolyBezier(hdc, pts, 4);
            break;
        case 1:  /* ) */
            pts[0].x = x;     pts[0].y = yTop;
            pts[1].x = x + w; pts[1].y = yTop + q;
            pts[2].x = x + w; pts[2].y = yBot - q;
            pts[3].x = x;     pts[3].y = yBot;
            PolyBezier(hdc, pts, 4);
            break;
        case 2:  /* [ */
            MoveToEx(hdc, x + w, yTop, NULL);
            LineTo(hdc, x, yTop);
            LineTo(hdc, x, yBot);
            LineTo(hdc, x + w, yBot);
            break;
        case 3:  /* ] */
            MoveToEx(hdc, x, yTop, NULL);
            LineTo(hdc, x + w, yTop);
            LineTo(hdc, x + w, yBot);
            LineTo(hdc, x, yBot);
            break;
        case 4: {  /* { ：上/下两段贝塞尔，中点尖角朝右 */
            POINT b1[4] = { { x + w, yTop }, { x, yTop + q }, { x, ym - q / 2 }, { x + w, ym } };
            PolyBezier(hdc, b1, 4);
            POINT b2[4] = { { x + w, ym }, { x, ym + q / 2 }, { x, yBot - q }, { x + w, yBot } };
            PolyBezier(hdc, b2, 4);
            break;
        }
        case 5: {  /* } */
            POINT b1[4] = { { x, yTop }, { x + w, yTop + q }, { x + w, ym - q / 2 }, { x, ym } };
            PolyBezier(hdc, b1, 4);
            POINT b2[4] = { { x, ym }, { x + w, ym + q / 2 }, { x + w, yBot - q }, { x, yBot } };
            PolyBezier(hdc, b2, 4);
            break;
        }
        case 6:  /* | */
            MoveToEx(hdc, x + w / 2, yTop, NULL);
            LineTo(hdc, x + w / 2, yBot);
            break;
        case 7:  /* ‖ */
            MoveToEx(hdc, x + w / 3, yTop, NULL);
            LineTo(hdc, x + w / 3, yBot);
            MoveToEx(hdc, x + 2 * w / 3, yTop, NULL);
            LineTo(hdc, x + 2 * w / 3, yBot);
            break;
    }
}

static void MbDraw(const MathBox* b, HDC hdc, int x, int yBase,
                   const MdFonts* f, const MdTheme* th) {
    switch (b->kind) {
        case MB_GLYPHS: {
            HFONT fo = MathFont(f, b->level, b->italic);
            HFONT of = (HFONT)SelectObject(hdc, fo);
            SetTextColor(hdc, th->fg);
            SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
            TextOutW(hdc, x, yBase, b->text, (int)wcslen(b->text));
            SelectObject(hdc, of);
            break;
        }
        case MB_ROW: {
            int cx = x;
            for (int i = 0; i < b->nKids; i++) {
                const MathBox* k = b->kids[i];
                if (!k) continue;
                MbDraw(k, hdc, cx, yBase, f, th);
                cx += k->w;
            }
            break;
        }
        case MB_FRAC: {
            const MathBox* num = b->nKids > 0 ? b->kids[0] : NULL;
            const MathBox* den = b->nKids > 1 ? b->kids[1] : NULL;
            int em = MathFontEm(MathFont(f, b->level, TRUE));
            int axis = yBase - em / 4;
            if (num)
                MbDraw(num, hdc, x + (b->w - num->w) / 2, axis - UI_Scale(2), f, th);
            HPEN pen = CreatePen(PS_SOLID, 1, th->fg);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, x + UI_Scale(2), axis, NULL);
            LineTo(hdc, x + b->w - UI_Scale(2), axis);
            SelectObject(hdc, op);
            DeleteObject(pen);
            if (den)
                MbDraw(den, hdc, x + (b->w - den->w) / 2, axis + UI_Scale(2) + den->asc, f, th);
            break;
        }
        case MB_SCRIPT: {
            const MathBox* base = b->nKids > 0 ? b->kids[0] : NULL;
            const MathBox* sup = b->nKids > 1 ? b->kids[1] : NULL;
            const MathBox* sub = b->nKids > 2 ? b->kids[2] : NULL;
            int bx = x;
            if (base) {
                MbDraw(base, hdc, bx, yBase, f, th);
                bx += base->w;
            }
            if (sup)
                MbDraw(sup, hdc, bx, yBase - b->raiseY, f, th);
            if (sub)
                MbDraw(sub, hdc, bx, yBase + sub->h - sub->asc + (int)(sub->h * 0.1), f, th);
            break;
        }
        case MB_RADICAL: {
            const MathBox* body = b->nKids > 0 ? b->kids[0] : NULL;
            int glyphW = UI_Scale(6 + b->level * 2);
            int top = yBase - b->asc;
            HPEN pen = CreatePen(PS_SOLID, (1 + b->level == 0 ? 1 : 1), th->fg);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, x, yBase - (b->h - b->asc) / 2 - (b->asc - (body ? body->asc : 0)) / 2, NULL);
            LineTo(hdc, x + glyphW / 3, yBase + UI_Scale(2));
            LineTo(hdc, x + glyphW, top + UI_Scale(1));
            SelectObject(hdc, op);
            DeleteObject(pen);
            HPEN pen2 = CreatePen(PS_SOLID, 1, th->fg);
            HPEN op2 = (HPEN)SelectObject(hdc, pen2);
            MoveToEx(hdc, x + glyphW, top + UI_Scale(1), NULL);
            LineTo(hdc, x + glyphW + UI_Scale(4) + (body ? body->w : 0) + UI_Scale(2), top + UI_Scale(1));
            SelectObject(hdc, op2);
            DeleteObject(pen2);
            if (body)
                MbDraw(body, hdc, x + glyphW + UI_Scale(4), yBase, f, th);
            break;
        }
        case MB_BIGOP: {
            const MathBox* g = b->nKids > 0 ? b->kids[0] : NULL;
            if (g) {
                MbDraw(g, hdc, x, yBase, f, th);
            }
            break;
        }
        case MB_ENV: {
            int nRows = b->nKids;
            int nCols = b->cols > 0 ? b->cols : 1;
            if (nCols > 16) nCols = 16;
            if (nRows > 64) nRows = 64;
            int colW[16];
            int rowH[64], rowAsc[64];
            for (int j = 0; j < nCols; j++) colW[j] = 0;
            for (int i = 0; i < nRows; i++) { rowH[i] = 0; rowAsc[i] = 0; }
            for (int i = 0; i < nRows; i++) {
                const MathBox* r = b->kids[i];
                if (!r) continue;
                for (int j = 0; j < r->nKids && j < nCols; j++) {
                    if (r->kids[j]->w > colW[j]) colW[j] = r->kids[j]->w;
                    if (r->kids[j]->h > rowH[i]) rowH[i] = r->kids[j]->h;
                    if (r->kids[j]->asc > rowAsc[i]) rowAsc[i] = r->kids[j]->asc;
                }
            }
            int cg = UI_Scale(8), rg = UI_Scale(4);
            int dw = UI_Scale(10);
            int top = yBase - b->asc;
            int yBot = top + b->h;
            HPEN pen = CreatePen(PS_SOLID, 1, th->fg);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            int cx = x;
            static const int kLeft[7]  = { -1, 0, 2, 4, 4, 6, 4 };  /* PLAIN,PAREN,BRACKET,BRACE,VBAR,VVBAR,CASES */
            static const int kRight[7] = { -1, 1, 3, 5, 6, 7, -1 };
            int li = kLeft[b->env & 7], ri = kRight[b->env & 7];
            if (li >= 0) { EnvDelim(hdc, cx, top, yBot, dw, li); cx += dw; }
            cx += UI_Scale(2);
            int colX[16];
            colX[0] = cx;
            for (int j = 1; j < nCols; j++) colX[j] = colX[j - 1] + colW[j - 1] + cg;
            int cy = top;
            for (int i = 0; i < nRows; i++) {
                int baseline = cy + rowAsc[i];
                const MathBox* r = b->kids[i];
                for (int j = 0; r && j < r->nKids && j < nCols; j++)
                    MbDraw(r->kids[j], hdc, colX[j], baseline, f, th);
                cy += rowH[i] + rg;
            }
            if (ri >= 0)
                EnvDelim(hdc, cx + (colX[nCols - 1] - colX[0]) + colW[nCols - 1] + UI_Scale(2),
                         top, yBot, dw, ri);
            SelectObject(hdc, op);
            DeleteObject(pen);
            break;
        }
    }
}

void Math_Draw(const MathBox* b, HDC hdc, int x, int yBase,
               const MdTheme* th, const MdFonts* f) {
    if (!b) return;
    SetBkMode(hdc, TRANSPARENT);
    MbDraw(b, hdc, x, yBase, f, th);
}

int Math_Width(const MathBox* b)  { return b ? b->w : 0; }
int Math_Height(const MathBox* b) { return b ? b->h : 0; }
int Math_Ascent(const MathBox* b) { return b ? b->asc : 0; }
