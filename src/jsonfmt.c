#include "common.h"

enum {
    JOK = 0,
    JERR_EMPTY,
    JERR_EOF,
    JERR_CHAR,
    JERR_STRING_CTRL,
    JERR_ESCAPE,
    JERR_UNICODE,
    JERR_NUMBER,
    JERR_LITERAL,
    JERR_KEY,
    JERR_COLON,
    JERR_COMMA_OBJ,
    JERR_COMMA_ARR,
    JERR_TRAILING,
    JERR_DEPTH,
    JERR_MEM
};

#define JMAX_DEPTH 500

typedef struct {
    const char* s;
    size_t n;
    size_t i;
    char* o;
    size_t ol, oc;
    BOOL minify;
    const char* nl;
    const char* ind;
    int depth;
    size_t errPos;
    int errCode;
} J;

static void jfail(J* j, size_t pos, int code) {
    j->errPos = pos; j->errCode = code;
}

static int jout(J* j, const char* p, size_t n) {
    if (j->ol + n + 1 > j->oc) {
        size_t nc = j->oc ? j->oc : 256;
        while (nc < j->ol + n + 1) nc *= 2;
        char* no = (char*)realloc(j->o, nc);
        if (!no) { jfail(j, j->i, JERR_MEM); return 0; }
        j->o = no; j->oc = nc;
    }
    memcpy(j->o + j->ol, p, n);
    j->ol += n;
    j->o[j->ol] = '\0';
    return 1;
}

static void jws(J* j) {
    while (j->i < j->n) {
        char c = j->s[j->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->i++;
        else break;
    }
}

static int jnlind(J* j, int depth) {
    if (j->minify) return 1;
    if (!jout(j, j->nl, strlen(j->nl))) return 0;
    for (int k = 0; k < depth; k++)
        if (!jout(j, j->ind, strlen(j->ind))) return 0;
    return 1;
}

static int jhex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int jstr(J* j) {
    size_t start = j->i;
    j->i++;
    for (;;) {
        if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
        unsigned char c = (unsigned char)j->s[j->i];
        if (c == '"') { j->i++; break; }
        if (c == '\\') {
            if (j->i + 1 >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
            char e = j->s[j->i + 1];
            if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' ||
                e == 'n' || e == 'r' || e == 't') {
                j->i += 2;
            } else if (e == 'u') {
                if (j->i + 5 >= j->n) { jfail(j, j->i, JERR_UNICODE); return 0; }
                for (int k = 2; k <= 5; k++)
                    if (!jhex(j->s[j->i + k])) { jfail(j, j->i, JERR_UNICODE); return 0; }
                j->i += 6;
            } else {
                jfail(j, j->i, JERR_ESCAPE); return 0;
            }
        } else if (c < 0x20) {
            jfail(j, j->i, JERR_STRING_CTRL); return 0;
        } else {
            j->i++;
        }
    }
    return jout(j, j->s + start, j->i - start);
}

static int jnum(J* j) {
    size_t start = j->i;
    if (j->i < j->n && j->s[j->i] == '-') j->i++;
    if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
    if (j->s[j->i] == '0') {
        j->i++;
        if (j->i < j->n && j->s[j->i] >= '0' && j->s[j->i] <= '9') {
            jfail(j, j->i, JERR_NUMBER); return 0;
        }
    } else if (j->s[j->i] >= '1' && j->s[j->i] <= '9') {
        while (j->i < j->n && j->s[j->i] >= '0' && j->s[j->i] <= '9') j->i++;
    } else {
        jfail(j, j->i, JERR_NUMBER); return 0;
    }
    if (j->i < j->n && j->s[j->i] == '.') {
        j->i++;
        if (j->i >= j->n || j->s[j->i] < '0' || j->s[j->i] > '9') {
            jfail(j, j->i, JERR_NUMBER); return 0;
        }
        while (j->i < j->n && j->s[j->i] >= '0' && j->s[j->i] <= '9') j->i++;
    }
    if (j->i < j->n && (j->s[j->i] == 'e' || j->s[j->i] == 'E')) {
        j->i++;
        if (j->i < j->n && (j->s[j->i] == '+' || j->s[j->i] == '-')) j->i++;
        if (j->i >= j->n || j->s[j->i] < '0' || j->s[j->i] > '9') {
            jfail(j, j->i, JERR_NUMBER); return 0;
        }
        while (j->i < j->n && j->s[j->i] >= '0' && j->s[j->i] <= '9') j->i++;
    }
    return jout(j, j->s + start, j->i - start);
}

static int jlit(J* j, const char* word) {
    size_t wl = strlen(word);
    if (j->i + wl > j->n || memcmp(j->s + j->i, word, wl) != 0) {
        jfail(j, j->i, JERR_LITERAL); return 0;
    }
    j->i += wl;
    return jout(j, word, wl);
}

static int jval(J* j);

static int jobj(J* j) {
    if (j->depth >= JMAX_DEPTH) { jfail(j, j->i, JERR_DEPTH); return 0; }
    j->i++;
    if (!jout(j, "{", 1)) return 0;
    j->depth++;
    jws(j);
    if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
    if (j->s[j->i] == '}') { j->i++; j->depth--; return jout(j, "}", 1); }
    for (;;) {
        if (!jnlind(j, j->depth)) return 0;
        if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
        if (j->s[j->i] != '"') { jfail(j, j->i, JERR_KEY); return 0; }
        if (!jstr(j)) return 0;
        jws(j);
        if (j->i >= j->n || j->s[j->i] != ':') { jfail(j, j->i, JERR_COLON); return 0; }
        j->i++;
        if (!jout(j, ":", 1)) return 0;
        if (!j->minify && !jout(j, " ", 1)) return 0;
        if (!jval(j)) return 0;
        jws(j);
        if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
        if (j->s[j->i] == ',') {
            j->i++;
            if (!jout(j, ",", 1)) return 0;
            jws(j);
            continue;
        }
        if (j->s[j->i] == '}') {
            j->i++;
            if (!jnlind(j, j->depth - 1)) return 0;
            j->depth--;
            return jout(j, "}", 1);
        }
        jfail(j, j->i, JERR_COMMA_OBJ); return 0;
    }
}

static int jarr(J* j) {
    if (j->depth >= JMAX_DEPTH) { jfail(j, j->i, JERR_DEPTH); return 0; }
    j->i++;
    if (!jout(j, "[", 1)) return 0;
    j->depth++;
    jws(j);
    if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
    if (j->s[j->i] == ']') { j->i++; j->depth--; return jout(j, "]", 1); }
    for (;;) {
        if (!jnlind(j, j->depth)) return 0;
        if (!jval(j)) return 0;
        jws(j);
        if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
        if (j->s[j->i] == ',') {
            j->i++;
            if (!jout(j, ",", 1)) return 0;
            jws(j);
            continue;
        }
        if (j->s[j->i] == ']') {
            j->i++;
            if (!jnlind(j, j->depth - 1)) return 0;
            j->depth--;
            return jout(j, "]", 1);
        }
        jfail(j, j->i, JERR_COMMA_ARR); return 0;
    }
}

static int jval(J* j) {
    jws(j);
    if (j->i >= j->n) { jfail(j, j->i, JERR_EOF); return 0; }
    char c = j->s[j->i];
    switch (c) {
        case '{': return jobj(j);
        case '[': return jarr(j);
        case '"': return jstr(j);
        case 't': return jlit(j, "true");
        case 'f': return jlit(j, "false");
        case 'n': return jlit(j, "null");
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return jnum(j);
            jfail(j, j->i, JERR_CHAR);
            return 0;
    }
}

static int Json_Reprint(const char* src, size_t len, int minify,
                        const char* nl, const char* ind,
                        char** out, size_t* outLen,
                        size_t* errPos, int* errCode) {
    J j; memset(&j, 0, sizeof(j));
    j.s = src; j.n = len; j.minify = minify; j.nl = nl; j.ind = ind;
    jws(&j);
    if (j.i >= j.n) { *errPos = j.i; *errCode = JERR_EMPTY; return 0; }
    if (!jval(&j)) { *errPos = j.errPos; *errCode = j.errCode; return 0; }
    jws(&j);
    if (j.i < j.n) { *errPos = j.i; *errCode = JERR_TRAILING; return 0; }
    for (size_t k = len; k > 0; k--) {
        char c = src[k - 1];
        if (c == ' ' || c == '\t') continue;
        if ((c == '\n' || c == '\r') && !jout(&j, nl, strlen(nl))) {
            *errPos = j.errPos; *errCode = j.errCode; return 0;
        }
        break;
    }
    *out = j.o; *outLen = j.ol;
    return 1;
}

static const wchar_t* JsonErrText(int code) {
    if (code >= JERR_EMPTY && code <= JERR_MEM)
        return T((StrId)(STR_JERR_EMPTY + (code - JERR_EMPTY)));
    return T(STR_JERR_UNKNOWN);
}

void Json_FormatActiveDoc(BOOL minify) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    Doc* d = &g_docs[g_curDoc];
    HWND hed = d->hwndEdit;
    Sci_Position docLen = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    Sci_Position selStart = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONEND, 0, 0);
    BOOL hasSel = (selEnd > selStart);
    Sci_Position start = hasSel ? selStart : 0;
    Sci_Position end = hasSel ? selEnd : docLen;
    if (end <= start) {
        MessageBoxW(g_hwndMain, T(STR_JSON_EMPTY),
                    minify ? T(STR_JSON_TITLE_MIN) : T(STR_JSON_TITLE_FMT), MB_ICONINFORMATION);
        return;
    }

    if (end - start > (Sci_Position)(100 * 1024 * 1024)) {
        wchar_t msg[128];
        wsprintf(msg, T(STR_MSG_JSON_BIG), 100);
        MessageBoxW(g_hwndMain, msg,
                    minify ? T(STR_JSON_TITLE_MIN) : T(STR_JSON_TITLE_FMT), MB_ICONINFORMATION);
        return;
    }

    Sci_Position rlen = end - start;
    char* src = (char*)malloc((size_t)rlen + 1);
    if (!src) return;
    struct Sci_TextRange tr;
    tr.chrg.cpMin = (Sci_PositionCR)start;
    tr.chrg.cpMax = (Sci_PositionCR)end;
    tr.lpstrText = src;
    SendMessage(hed, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);

    char ind[16];
    int useTabs = (int)SendMessage(hed, SCI_GETUSETABS, 0, 0);
    int tabW = (int)SendMessage(hed, SCI_GETTABWIDTH, 0, 0);
    if (tabW < 1) tabW = 4;
    if (tabW > 12) tabW = 12;
    if (useTabs) {
        strcpy_s(ind, sizeof(ind), "\t");
    } else {
        for (int k = 0; k < tabW; k++) ind[k] = ' ';
        ind[tabW] = '\0';
    }
    const char* nl = (d->eol == 1) ? "\n" : ((d->eol == 2) ? "\r" : "\r\n");

    char* out = NULL; size_t outLen = 0; size_t errPos = 0; int errCode = JOK;
    int ok = Json_Reprint(src, (size_t)rlen, minify, nl, ind, &out, &outLen, &errPos, &errCode);
    free(src);
    if (!ok) {
        Sci_Position abs = start + (Sci_Position)errPos;
        if (abs > docLen) abs = docLen;
        int line = (int)SendMessage(hed, SCI_LINEFROMPOSITION, abs, 0);
        int col = (int)(abs - (Sci_Position)SendMessage(hed, SCI_POSITIONFROMLINE, line, 0)) + 1;
        wchar_t msg[256];
        _snwprintf(msg, 256, T(STR_JSON_ERR_FMT),
                   line + 1, col, JsonErrText(errCode));
        msg[255] = L'\0';
        MessageBoxW(g_hwndMain, msg, minify ? T(STR_JSON_TITLE_MIN) : T(STR_JSON_TITLE_FMT), MB_ICONERROR);
        SendMessage(hed, SCI_GOTOPOS, abs, 0);
        SendMessage(hed, SCI_SETSEL, abs, (abs < docLen) ? abs + 1 : abs);
        SendMessage(hed, SCI_SCROLLCARET, 0, 0);
        return;
    }

    SendMessage(hed, SCI_BEGINUNDOACTION, 0, 0);
    SendMessage(hed, SCI_SETTARGETSTART, start, 0);
    SendMessage(hed, SCI_SETTARGETEND, end, 0);
    SendMessage(hed, SCI_REPLACETARGET, (WPARAM)outLen, (LPARAM)out);
    SendMessage(hed, SCI_ENDUNDOACTION, 0, 0);
    SendMessage(hed, SCI_SETSEL, start, start);
    SendMessage(hed, SCI_SCROLLCARET, 0, 0);
    free(out);
}
