/* mdhl.c — Markdown 预览代码块的语法着色（轻量词法分析）
 *
 * MdHl_Tokenize 把代码文本切为带类型的高亮片段（CodeSpan，仅输出非普通
 * 文本片段，按 pos 升序、互不重叠），由 mdview 的 LayoutCodeBlock 转成
 * 彩色 run。支持：C/C++/C#/Java/JS/TS/Go/Rust（C 风格）、Python、JSON、
 * XML/HTML、SQL、Bash。纯函数、无全局状态。
 */
#include "common.h"
#include <wchar.h>
#include <string.h>

/* ---------------- 语言关键字表 ---------------- */

enum { HLNG_C = 0, HLNG_PY, HLNG_JSON, HLNG_XML, HLNG_SQL, HLNG_SH };

static const wchar_t* const kwC[] = {
    L"auto", L"bool", L"break", L"case", L"catch", L"char", L"class",
    L"const", L"constexpr", L"continue", L"default", L"delete", L"do",
    L"double", L"else", L"enum", L"extern", L"false", L"float", L"for",
    L"friend", L"goto", L"if", L"inline", L"int", L"long", L"namespace",
    L"new", L"nullptr", L"NULL", L"operator", L"override", L"private",
    L"protected", L"public", L"register", L"return", L"short", L"signed",
    L"sizeof", L"static", L"struct", L"switch", L"template", L"this",
    L"throw", L"true", L"try", L"typedef", L"typename", L"union",
    L"unsigned", L"using", L"virtual", L"void", L"volatile", L"while",
    NULL
};
static const wchar_t* const kwCs[] = {
    L"abstract", L"as", L"async", L"await", L"base", L"bool", L"break",
    L"byte", L"case", L"catch", L"char", L"checked", L"class", L"const",
    L"continue", L"decimal", L"default", L"delegate", L"do", L"double",
    L"else", L"enum", L"event", L"explicit", L"extern", L"false", L"finally",
    L"fixed", L"float", L"for", L"foreach", L"get", L"goto", L"if",
    L"implicit", L"in", L"int", L"interface", L"internal", L"is", L"lock",
    L"long", L"namespace", L"new", L"null", L"object", L"operator", L"out",
    L"override", L"params", L"private", L"protected", L"public", L"readonly",
    L"ref", L"return", L"sbyte", L"sealed", L"set", L"short", L"sizeof",
    L"stackalloc", L"static", L"string", L"struct", L"switch", L"this",
    L"throw", L"true", L"try", L"typeof", L"uint", L"ulong", L"unchecked",
    L"unsafe", L"ushort", L"using", L"value", L"var", L"virtual", L"void",
    L"volatile", L"while", NULL
};
static const wchar_t* const kwJava[] = {
    L"abstract", L"assert", L"boolean", L"break", L"byte", L"case", L"catch",
    L"char", L"class", L"const", L"continue", L"default", L"do", L"double",
    L"else", L"enum", L"extends", L"final", L"finally", L"float", L"for",
    L"goto", L"if", L"implements", L"import", L"instanceof", L"int",
    L"interface", L"long", L"native", L"new", L"package", L"private",
    L"protected", L"public", L"return", L"short", L"static", L"strictfp",
    L"super", L"switch", L"synchronized", L"this", L"throw", L"throws",
    L"transient", L"try", L"void", L"volatile", L"while", L"true", L"false",
    L"null", NULL
};
static const wchar_t* const kwJs[] = {
    L"async", L"await", L"break", L"case", L"catch", L"class", L"const",
    L"continue", L"debugger", L"default", L"delete", L"do", L"else",
    L"enum", L"export", L"extends", L"false", L"finally", L"for", L"from",
    L"function", L"get", L"if", L"implements", L"import", L"in",
    L"instanceof", L"interface", L"let", L"new", L"null", L"of", L"private",
    L"protected", L"public", L"readonly", L"return", L"set", L"static",
    L"super", L"switch", L"this", L"throw", L"true", L"try", L"type",
    L"typeof", L"undefined", L"var", L"void", L"while", L"yield", NULL
};
static const wchar_t* const kwGo[] = {
    L"break", L"byte", L"cap", L"case", L"chan", L"const", L"continue",
    L"default", L"defer", L"delete", L"else", L"error", L"fallthrough",
    L"false", L"float32", L"float64", L"for", L"func", L"go", L"goto", L"if",
    L"import", L"int", L"int8", L"int16", L"int32", L"int64", L"interface",
    L"len", L"make", L"map", L"new", L"nil", L"package", L"panic", L"print",
    L"println", L"range", L"recover", L"return", L"rune", L"select",
    L"string", L"struct", L"switch", L"true", L"type", L"uint", L"uint8",
    L"uint64", L"var", NULL
};
static const wchar_t* const kwRust[] = {
    L"as", L"async", L"await", L"break", L"const", L"continue", L"crate",
    L"dyn", L"else", L"enum", L"Err", L"extern", L"false", L"fn", L"for",
    L"if", L"impl", L"in", L"let", L"loop", L"match", L"mod", L"move", L"mut",
    L"None", L"Ok", L"pub", L"ref", L"return", L"self", L"Self", L"static",
    L"struct", L"super", L"trait", L"true", L"type", L"unsafe", L"use",
    L"where", L"while", NULL
};
static const wchar_t* const kwPy[] = {
    L"and", L"as", L"assert", L"async", L"await", L"break", L"class",
    L"continue", L"def", L"del", L"elif", L"else", L"except", L"False",
    L"finally", L"for", L"from", L"global", L"if", L"import", L"in", L"is",
    L"lambda", L"None", L"nonlocal", L"not", L"or", L"pass", L"raise",
    L"return", L"True", L"try", L"while", L"with", L"yield", NULL
};
static const wchar_t* const kwSql[] = {
    L"ADD", L"ALL", L"ALTER", L"AND", L"AS", L"ASC", L"AVG", L"BEGIN",
    L"BETWEEN", L"BY", L"CASE", L"CHAR", L"CHECK", L"COMMIT", L"CONSTRAINT",
    L"COUNT", L"CREATE", L"DEFAULT", L"DELETE", L"DESC", L"DISTINCT", L"DROP",
    L"ELSE", L"END", L"EXISTS", L"FOREIGN", L"FROM", L"FULL", L"GROUP",
    L"HAVING", L"IN", L"INDEX", L"INNER", L"INSERT", L"INTO", L"IS", L"JOIN",
    L"KEY", L"LEFT", L"LIKE", L"LIMIT", L"MAX", L"MIN", L"NOT", L"NULL",
    L"OFFSET", L"ON", L"OR", L"ORDER", L"OUTER", L"PRIMARY", L"REFERENCES",
    L"ROLLBACK", L"SELECT", L"SET", L"SUM", L"TABLE", L"THEN", L"TOP",
    L"TRANSACTION", L"UNION", L"UNIQUE", L"UPDATE", L"VALUES", L"VIEW",
    L"WHEN", L"WHERE", L"WITH", NULL
};
static const wchar_t* const kwSh[] = {
    L"alias", L"break", L"case", L"cd", L"continue", L"do", L"done", L"echo",
    L"elif", L"else", L"esac", L"exit", L"export", L"fi", L"for", L"function",
    L"if", L"in", L"local", L"printf", L"readonly", L"return", L"select",
    L"set", L"shift", L"source", L"then", L"trap", L"unset", L"until",
    L"while", NULL
};
static const wchar_t* const kwJson[] = { L"true", L"false", L"null", NULL };

/* C 风格语言的差异配置：行注释 / 块注释 / 字符串引号 / 预处理 */
typedef struct {
    const wchar_t*const* kw;
    BOOL kwCI;            /* 关键字大小写不敏感 */
    const wchar_t* lineCom;
    const wchar_t* bOpen, *bClose;
    const wchar_t* quotes;
    BOOL esc;             /* 字符串内反斜杠转义 */
    BOOL triple;          /* Python 三引号 */
    BOOL preproc;         /* 行首 # 为预处理指令 */
} HlCfg;

static const HlCfg kCfgs[] = {
    [HLNG_C]  = { kwC,    FALSE, L"//", L"/*", L"*/", L"\"'", TRUE,  FALSE, TRUE },
    [HLNG_PY] = { kwPy,   FALSE, L"#",  NULL, NULL,  L"\"'", TRUE,  TRUE,  FALSE },
    [HLNG_JSON]={ kwJson, FALSE, NULL, NULL, NULL,  L"\"",  TRUE,  FALSE, FALSE },
    [HLNG_SQL]= { kwSql,  TRUE,  L"--", L"/*", L"*/", L"'\"", FALSE, FALSE, FALSE },
    [HLNG_SH] = { kwSh,   FALSE, L"#",  NULL, NULL,  L"\"'", TRUE,  FALSE, FALSE },
};

static int LangFromFence(const char* lang) {
    if (!lang || !lang[0]) return -1;
    static const struct { const char* fence; int lang; } map[] = {
        { "c", HLNG_C }, { "h", HLNG_C },
        { "cpp", HLNG_C }, { "c++", HLNG_C }, { "cxx", HLNG_C }, { "cc", HLNG_C },
        { "hpp", HLNG_C },
        { "csharp", HLNG_C }, { "cs", HLNG_C },
        { "java", HLNG_C },
        { "javascript", HLNG_C }, { "js", HLNG_C }, { "jsx", HLNG_C },
        { "typescript", HLNG_C }, { "ts", HLNG_C }, { "tsx", HLNG_C },
        { "go", HLNG_C }, { "golang", HLNG_C },
        { "rust", HLNG_C }, { "rs", HLNG_C },
        { "python", HLNG_PY }, { "py", HLNG_PY }, { "python3", HLNG_PY },
        { "json", HLNG_JSON }, { "json5", HLNG_JSON }, { "jsonc", HLNG_JSON },
        { "xml", HLNG_XML }, { "html", HLNG_XML }, { "htm", HLNG_XML },
        { "xhtml", HLNG_XML }, { "svg", HLNG_XML }, { "csproj", HLNG_XML },
        { "sql", HLNG_SQL },
        { "bash", HLNG_SH }, { "sh", HLNG_SH }, { "shell", HLNG_SH },
        { "zsh", HLNG_SH },
    };
    char buf[24];
    int i = 0;
    for (; lang[i] && i < 23; i++) {
        char c = lang[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        buf[i] = c;
    }
    buf[i] = 0;
    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
        if (strcmp(buf, map[i].fence) == 0) return map[i].lang;
    return -1;
}

static const HlCfg* CfgFor(int lang) {
    if (lang == HLNG_XML) return NULL;
    return &kCfgs[lang];
}

/* ---------------- 输出收集 ---------------- */

typedef struct {
    CodeSpan* v; int n, cap;
} Spans;

static void Emit(Spans* s, int pos, int len, int type) {
    if (len <= 0) return;
    if (s->n && s->v[s->n - 1].pos + s->v[s->n - 1].len == pos &&
        s->v[s->n - 1].type == (unsigned char)type) {   /* 相邻同类型合并 */
        s->v[s->n - 1].len += len;
        return;
    }
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->v = (CodeSpan*)realloc(s->v, (size_t)s->cap * sizeof(CodeSpan));
        if (!s->v) { s->cap = 0; s->n = 0; return; }
    }
    s->v[s->n].pos = pos;
    s->v[s->n].len = len;
    s->v[s->n].type = (unsigned char)type;
    s->n++;
}

static BOOL StartsWith(const wchar_t* s, int len, int i, const wchar_t* pre) {
    for (int k = 0; pre[k]; k++) {
        if (i + k >= len || s[i + k] != pre[k]) return FALSE;
    }
    return TRUE;
}

static BOOL IsIdentCh(wchar_t c, int first) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' ||
           c == L'$' || (c >= L'0' && c <= L'9' && !first) || c > 0x7F /* 允许 Unicode 标识符 */;
}

static BOOL KwHit(const wchar_t*const* kw, BOOL ci, const wchar_t* w, int len) {
    for (int i = 0; kw[i]; i++) {
        int kl = (int)wcslen(kw[i]);
        if (kl != len) continue;
        BOOL eq = TRUE;
        for (int j = 0; j < len; j++) {
            wchar_t a = w[j], b = kw[i][j];
            if (ci && a >= L'A' && a <= L'Z') a += 32;
            if (a != b) { eq = FALSE; break; }
        }
        if (eq) return TRUE;
    }
    return FALSE;
}

/* ---------------- C 风格 / Python / SQL / Bash / JSON 通用扫描 ---------------- */

static void ScanGeneric(const wchar_t* s, int len, const HlCfg* g, Spans* out) {
    BOOL preLine = TRUE;
    int i = 0;
    while (i < len) {
        wchar_t c = s[i];
        if (c == L'\n') { preLine = TRUE; i++; continue; }
        if (c == L' ' || c == L'\t' || c == L'\r') { i++; continue; }
        /* 行首预处理（C 系）：#include/#define... 整行 */
        if (g->preproc && preLine && c == L'#') {
            int j = i;
            while (j < len && s[j] != L'\n') j++;
            Emit(out, i, j - i, HL_PRE);
            i = j;
            continue;
        }
        preLine = FALSE;
        if (g->lineCom && StartsWith(s, len, i, g->lineCom)) {
            int j = i;
            while (j < len && s[j] != L'\n') j++;
            Emit(out, i, j - i, HL_COM);
            i = j;
            continue;
        }
        if (g->bOpen && StartsWith(s, len, i, g->bOpen)) {
            int j = i + (int)wcslen(g->bOpen);
            while (j < len && !StartsWith(s, len, j, g->bClose)) j++;
            if (j < len) j += (int)wcslen(g->bClose);
            Emit(out, i, j - i, HL_COM);
            i = j;
            continue;
        }
        /* 三引号（Python）优先 */
        if (g->triple && (c == L'"' || c == L'\'') && i + 2 < len &&
            s[i+1] == c && s[i+2] == c) {
            int j = i + 3;
            while (j < len) {
                if (j + 2 < len && s[j] == c && s[j+1] == c && s[j+2] == c) {
                    j += 3; break;
                }
                j++;
            }
            Emit(out, i, j - i, HL_STR);
            i = j;
            continue;
        }
        if (g->quotes && wcschr(g->quotes, c) && c) {
            wchar_t q = c;
            int j = i + 1;
            while (j < len && s[j] != q && s[j] != L'\n') {
                if (g->esc && s[j] == L'\\' && j + 1 < len) j++;
                j++;
            }
            if (j < len && s[j] == q) j++;
            Emit(out, i, j - i, HL_STR);
            i = j;
            continue;
        }
        if (c >= L'0' && c <= L'9' &&
            !(i > 0 && IsIdentCh(s[i-1], FALSE))) {
            int j = i + 1;
            while (j < len && ((s[j] >= L'0' && s[j] <= L'9') ||
                   (s[j] >= L'a' && s[j] <= L'f') || (s[j] >= L'A' && s[j] <= L'F') ||
                   s[j] == L'.' || s[j] == L'x' || s[j] == L'X' || s[j] == L'_' ||
                   s[j] == L'+' || s[j] == L'-')) {
                if ((s[j] == L'+' || s[j] == L'-') &&
                    !(j > 0 && (s[j-1] == L'e' || s[j-1] == L'E'))) break;
                j++;
            }
            Emit(out, i, j - i, HL_NUM);
            i = j;
            continue;
        }
        if (IsIdentCh(c, TRUE)) {
            int j = i + 1;
            while (j < len && IsIdentCh(s[j], FALSE)) j++;
            if (KwHit(g->kw, g->kwCI, s + i, j - i))
                Emit(out, i, j - i, HL_KW);
            i = j;
            continue;
        }
        i++;
    }
}

/* ---------------- XML/HTML ---------------- */

static void ScanXml(const wchar_t* s, int len, Spans* out) {
    int i = 0;
    while (i < len) {
        if (StartsWith(s, len, i, L"<!--")) {
            int j = i + 4;
            while (j < len && !StartsWith(s, len, j, L"-->")) j++;
            if (j < len) j += 3;
            Emit(out, i, j - i, HL_COM);
            i = j;
            continue;
        }
        if (s[i] == L'<') {
            int j = i + 1;
            if (j < len && s[j] == L'/') j++;
            int ns = j;
            while (j < len && s[j] != L'>' && s[j] != L'\n' &&
                   !IsIdentCh(s[j], FALSE) && s[j] != L':') j++;  /* 名字（含冒号） */
            if (j > ns) Emit(out, ns, j - ns, HL_KW);
            int tagEnd = j;
            while (tagEnd < len && s[tagEnd] != L'>' && s[tagEnd] != L'\n') tagEnd++;
            /* 标签内部：属性名 / 字符串 */
            int k = j;
            while (k < tagEnd) {
                if (s[k] == L'"' || s[k] == L'\'') {
                    wchar_t q = s[k];
                    int m = k + 1;
                    while (m < tagEnd && s[m] != q) m++;
                    if (m < tagEnd) m++;
                    Emit(out, k, m - k, HL_STR);
                    k = m;
                    continue;
                }
                k++;
            }
            i = (tagEnd < len && s[tagEnd] == L'>') ? tagEnd + 1 : tagEnd;
            continue;
        }
        i++;
    }
}

/* ---------------- 入口 ---------------- */

int MdHl_Tokenize(const wchar_t* code, int len, const char* lang, CodeSpan** out) {
    *out = NULL;
    if (!code || len <= 0) return 0;
    int lg = LangFromFence(lang);
    if (lg < 0) return 0;
    Spans s = { NULL, 0, 0 };
    if (lg == HLNG_XML) ScanXml(code, len, &s);
    else {
        /* C 家族关键字按语言选表 */
        HlCfg cfg = kCfgs[lg];
        if (lg == HLNG_C) {
            char l24[24]; int i;
            for (i = 0; lang[i] && i < 23; i++) l24[i] = lang[i];
            l24[i] = 0;
            if (!strcmp(l24, "cs") || !strcmp(l24, "csharp")) cfg.kw = kwCs;
            else if (!strcmp(l24, "java")) cfg.kw = kwJava;
            else if (!strcmp(l24, "js") || !strcmp(l24, "jsx") ||
                     !strcmp(l24, "javascript") || !strcmp(l24, "ts") ||
                     !strcmp(l24, "tsx") || !strcmp(l24, "typescript")) cfg.kw = kwJs;
            else if (!strcmp(l24, "go") || !strcmp(l24, "golang")) cfg.kw = kwGo;
            else if (!strcmp(l24, "rs") || !strcmp(l24, "rust")) cfg.kw = kwRust;
        }
        ScanGeneric(code, len, &cfg, &s);
    }
    *out = s.v;
    return s.n;
}
