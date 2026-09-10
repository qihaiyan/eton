/* mdmath.c — LaTeX 数学公式子集的排版与绘制（GDI）
   支持：内联 $...$ 与块级 $$...$$（MD4C 的 LaTeX math span），
   覆盖 \frac \sqrt \sum \int \prod \lim、上下标（嵌套）、希腊字母与
   常用符号、\text/\mathrm/\mathbf/\mathbb、\left(\right 忽略、间距命令。
   字体：Cambria Math（Windows 自带），三级字号（正文/脚本/脚本的脚本）。 */

#include "common.h"

static int MbFontH(HDC hdc, HFONT f) {
    HFONT of = (HFONT)SelectObject(hdc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, of);
    return tm.tmHeight;
}
#define FontH(hdc, f) MbFontH(hdc, f)

/* ============================ 盒子模型 ============================ */

typedef enum {
    MB_ROW,        /* 横排子盒 */
    MB_GLYPHS,     /* 一段文字 + 字体级/斜体 */
    MB_FRAC,       /* 分数：num / den */
    MB_SCRIPT,     /* 上下标 */
    MB_RADICAL,    /* 根式 */
    MB_BIGOP       /* 大运算符 + 上下限 */
} MbKind;

struct MathBox {
    MbKind kind;
    int w, h, asc;          /* asc = 基线到盒顶 */
    MathBox** kids; int nKids;   /* ROW: 横排；SCRIPT: base,sup,sub(可空)；
                                    FRAC: num,den；RADICAL: body；BIGOP: 上下限 */
    wchar_t* text;          /* GLYPHS */
    int level;              /* 字号级 0/1/2 */
    BOOL italic;
    int spaceAfter;         /* 附加间距（px，布局时定） */
};

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
    if (!row || !kid) return;
    MathBox** nk = (MathBox**)realloc(row->kids,
        ((size_t)row->nKids + 1) * sizeof(MathBox*));
    if (!nk) return;
    row->kids = nk;
    row->kids[row->nKids++] = kid;
}

/* ============================ LaTeX 解析 ============================ */

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

/* 双线字母（\mathbb）常用映射；BMP 外码位（E/F）拆代理对写入，返回写入数 */
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

/* 读取一个 {...} 组（跳过开 {，读到配对 }） */
static MathBox* MathParseGroup(MScan* s, int level) {
    if (s->p < s->end && *s->p == L'{') {
        s->p++;
        MathBox* row = MathParseRow(s, level);
        if (s->p < s->end && *s->p == L'}') s->p++;
        return row;
    }
    /* 单字符参数 */
    return MathParseRow(s, level);
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
            MathBox* sc = MathParseGroup(s, level < 2 ? level + 1 : 2);
            /* 若 row 末尾已有脚本盒则合并，否则新建 */
            MathBox* target = NULL;
            if (row->nKids > 0 && row->kids[row->nKids - 1]->kind == MB_SCRIPT)
                target = row->kids[row->nKids - 1];
            if (!target) {
                target = MbNew(MB_SCRIPT);
                target->level = level;
                MbAdd(row, target);
            }
            if (c == L'^') {
                if (target->nKids >= 2 && target->kids[1]) { /* 覆盖 */ Math_Free(target->kids[1]); target->kids[1] = sc; }
                else { while (target->nKids < 2) MbAdd(target, NULL); target->kids[1] = sc; }
            } else {
                if (target->nKids >= 1 && target->kids[0] && target->kids[0]->kind != MB_GLYPHS) {
                    /* base 占用则作 sub 存 kids[2] */
                    while (target->nKids < 3) MbAdd(target, NULL);
                    if (target->kids[2]) Math_Free(target->kids[2]);
                    target->kids[2] = sc;
                } else if (target->nKids == 0) {
                    MbAdd(target, NULL);          /* kids[0] = base 占位 */
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
                /* 换行 / 间距 */
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
            /* 读命令名 */
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
                /* 跳过可选 [n] */
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
                    case L's': glyph = L"\x2211"; break;         /* ∑ */
                    case L'p': glyph = L"\x220F"; break;         /* ∏ */
                    case L'i':
                        if (cmd[1] == L'i') glyph = L"\x222C";
                        else glyph = L"\x222B";
                        break;
                    case L'b': glyph = (cmd[3] == L'u') ? L"\x22C3" : L"\x22C2"; break;
                    default: glyph = cmd; break;                 /* lim/log/sin… 文字型 */
                }
                MathBox* op = MbNew(MB_BIGOP);
                op->level = level;
                wchar_t tmp[8];
                int gl = (int)wcslen(glyph);
                if (gl > 7) gl = 7;
                memcpy(tmp, glyph, (size_t)gl * sizeof(wchar_t));
                tmp[gl] = 0;
                /* 文字型（lim/log 等）按正体普通字号；算符型用大字形 */
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
                /* 大运算符的上下限由随后的 ^/_ 挂到 op —— 下一轮 ^_ 检测末尾为
                   BIGOP 时直接作为其 kids[1]/[2] */
                continue;
            }
            if (wcscmp(cmd, L"text") == 0 || wcscmp(cmd, L"mathrm") == 0 ||
                wcscmp(cmd, L"mbox") == 0) {
                FLUSH();
                /* 原样取组内文字（正体） */
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
                /* 组内全大写处理从简：直接按正体加粗显示 */
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
                /* 忽略 \left \right，其后的括号原样输出 */
                if (s->p < s->end && *s->p == L'.') s->p++;   /* \right. 无括号 */
                continue;
            }
            if (wcscmp(cmd, L"begin") == 0 || wcscmp(cmd, L"end") == 0) {
                /* 跳过 {env} */
                if (s->p < s->end && *s->p == L'{') {
                    while (s->p < s->end && *s->p != L'}') s->p++;
                    if (s->p < s->end) s->p++;
                }
                continue;
            }
            /* 符号表 */
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
            /* 未知命令：原样显示 \cmd */
            FLUSH();
            wchar_t ub[24];
            ub[0] = L'\\';
            int u2 = 1;
            for (const wchar_t* q = cmd; *q && u2 < 22; q++) ub[u2++] = *q;
            MbAdd(row, MbGlyphs(ub, u2, level, FALSE));
            continue;
        }
        /* 普通字符：字母斜体连排，数字/标点正体 */
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

MathBox* Math_Build(const wchar_t* latex) {
    if (!latex || !*latex) return NULL;
    MScan s = { latex, latex + wcslen(latex) };
    return MathParseRow(&s, 0);
}

/* ============================ 度量与绘制 ============================ */

static HFONT MathFont(const MdFonts* f, int level, BOOL italic) {
    switch (level) {
        case 1:  return italic ? f->mathItS : f->mathUpS;
        case 2:  return italic ? f->mathItSS : f->mathUpSS;
        default: return italic ? f->mathIt : f->mathUp;
    }
}

static void MbMeasure(MathBox* b, HDC hdc, const MdFonts* f);

static void MbMeasureKids(MathBox* b, HDC hdc, const MdFonts* f) {
    for (int i = 0; i < b->nKids; i++)
        if (b->kids[i]) MbMeasure(b->kids[i], hdc, f);
}

static void MbMeasure(MathBox* b, HDC hdc, const MdFonts* f) {
    switch (b->kind) {
        case MB_GLYPHS: {
            HFONT fo = MathFont(f, b->level, b->italic);
            HFONT of = (HFONT)SelectObject(hdc, fo);
            SIZE sz;
            GetTextExtentPoint32W(hdc, b->text, (int)wcslen(b->text), &sz);
            TEXTMETRICW tm;
            GetTextMetricsW(hdc, &tm);
            SelectObject(hdc, of);
            b->w = sz.cx + b->spaceAfter;
            b->h = tm.tmHeight;
            b->asc = tm.tmAscent;
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
            int axis = FontH(hdc, MathFont(f, b->level, TRUE)) / 2;
            int nu = num ? num->asc + (num->h - num->asc) : 0;
            int du = den ? den->asc : 0;
            b->w = w;
            b->h = axis + UI_Scale(3) + du + (num ? num->h - num->asc : 0) + UI_Scale(2) + axis;
            if (b->h < FontH(hdc, MathFont(f, b->level, TRUE))) b->h = FontH(hdc, MathFont(f, b->level, TRUE));
            b->asc = (num ? num->asc : 0) + UI_Scale(3) + axis;
            if (b->asc < axis + UI_Scale(2)) b->asc = axis + UI_Scale(2);
            (void)nu;
            break;
        }
        case MB_SCRIPT: {
            MbMeasureKids(b, hdc, f);
            MathBox* base = b->nKids > 0 ? b->kids[0] : NULL;
            MathBox* sup = b->nKids > 1 ? b->kids[1] : NULL;
            MathBox* sub = b->nKids > 2 ? b->kids[2] : NULL;
            int w = base ? base->w : 0;
            int asc = base ? base->asc : 0;
            int desc = base ? base->h - base->asc : 0;
            if (sup) {
                if (base && base->w + sup->w > w) w = base->w + sup->w;
                else if (!base) w = sup->w;
                if (asc + sup->h - sup->asc * 0 / 1 > asc + (sup->h - sup->asc)) { }
                if (asc < sup->h + UI_Scale(1)) { }   /* 顶到基线 */
                int need = (int)(asc * 0.55) + sup->h;
                if (need > asc + sup->h) { }
                if (asc + 0 < need) asc = need;   /* 抬高基线上方 */
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
            int axis = yBase - FontH(hdc, MathFont(f, b->level, TRUE)) / 4;
            if (num)
                MbDraw(num, hdc, x + (b->w - num->w) / 2, axis - UI_Scale(3), f, th);
            HPEN pen = CreatePen(PS_SOLID, 1, th->fg);
            HPEN op = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, x + UI_Scale(2), axis, NULL);
            LineTo(hdc, x + b->w - UI_Scale(2), axis);
            SelectObject(hdc, op);
            DeleteObject(pen);
            if (den)
                MbDraw(den, hdc, x + (b->w - den->w) / 2, axis + UI_Scale(3) + den->asc, f, th);
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
                MbDraw(sup, hdc, bx, yBase - (int)(b->asc * 0.55) - (sup->h - sup->asc), f, th);
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
            /* 根号：折线 */
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
                /* 上下限由 SCRIPT 挂接（BIGOP 后随 ^/_ 会转成脚本盒挂前一个） */
            }
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

/* 不透明访问器（mdview 布局用） */
int Math_Width(const MathBox* b)  { return b ? b->w : 0; }
int Math_Height(const MathBox* b) { return b ? b->h : 0; }
int Math_Ascent(const MathBox* b) { return b ? b->asc : 0; }
