#include "common.h"

static MdSrcBlock* BlkPush(MdSrcBlock* v, int* n, int* cap, int start, int end) {
    if (*n >= *cap) {
        int nc = *cap ? *cap * 2 : 16;
        MdSrcBlock* nv = (MdSrcBlock*)realloc(v, (size_t)nc * sizeof(MdSrcBlock));
        if (!nv) { free(v); return NULL; }
        v = nv;
        *cap = nc;
    }
    v[*n].start = start;
    v[*n].end = end;
    (*n)++;
    return v;
}
int MdEditScanBlocks(const char* s, int len, MdSrcBlock** out) {
    *out = NULL;
    if (!s || len <= 0) return 0;
    MdSrcBlock* v = NULL;
    int n = 0, cap = 0;
    int runStart = -1;
    int fenceStart = -1;
    char fenceChar = 0;
    int ls = 0;
    while (ls < len) {
        int le = ls;
        while (le < len && s[le] != '\n') le++;
        int contentEnd = le;
        while (contentEnd > ls && s[contentEnd-1] == '\r') contentEnd--;
        int p = ls;
        while (p < contentEnd && (s[p] == ' ' || s[p] == '\t')) p++;
        BOOL blank = (p >= contentEnd);
        if (fenceStart >= 0) {
            if (!blank && s[p] == fenceChar) {
                int q = p;
                while (q < contentEnd && s[q] == fenceChar) q++;
                if (q - p >= 3) {
                    v = BlkPush(v, &n, &cap, fenceStart, (le < len) ? le + 1 : len);
                    if (!v) return 0;
                    fenceStart = -1;
                    runStart = -1;
                }
            }
        } else if (!blank) {
            BOOL isFence = FALSE;
            if (s[p] == '`' || s[p] == '~') {
                int q = p;
                while (q < contentEnd && s[q] == s[p]) q++;
                if (q - p >= 3 && (q >= contentEnd || s[q] == ' ' || s[q] == '\t')) {
                    isFence = TRUE;
                    fenceStart = ls;
                    fenceChar = s[p];
                    runStart = -1;
                }
            }
            if (!isFence && runStart < 0) runStart = ls;
        } else {
            runStart = -1;
        }
        ls = le + 1;
    }
    if (fenceStart >= 0) {
        v = BlkPush(v, &n, &cap, fenceStart, len);
    } else if (runStart >= 0) {
        int e = len;
        while (e > runStart && (s[e-1] == '\n' || s[e-1] == '\r')) e--;
        if (e < len) e++;
        v = BlkPush(v, &n, &cap, runStart, e);
    }
    *out = v;
    return v ? n : 0;
}

static struct {
    HWND hed;
    int doc;
    int start, end;
    char* snap;
    int snapLen;
    BOOL isNew;
    BOOL committing;
} E = { NULL, -1, 0, 0, NULL, 0, FALSE, FALSE };

static void TakeSnapshot(void) {
    E.snapLen = E.end - E.start;
    E.snap = NULL;
    if (E.snapLen <= 0) { E.snapLen = 0; return; }
    E.snap = (char*)malloc((size_t)E.snapLen);
    if (!E.snap) { E.snapLen = 0; return; }
    struct Sci_TextRange tr;
    tr.chrg.cpMin = E.start;
    tr.chrg.cpMax = E.end;
    tr.lpstrText = E.snap;
    SendMessage(g_docs[E.doc].hwndEdit, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
}
static const char* DocEolSeq(void) {
    int e = (E.doc >= 0 && E.doc < g_docCount) ? g_docs[E.doc].eol : 1;
    return e == 0 ? "\r\n" : "\n";
}
static LRESULT CALLBACK OverlaySubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR id, DWORD_PTR ref) {
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE && E.doc >= 0) {
        MdEdit_Commit(TRUE);
        return 0;
    }
    if (msg == WM_KILLFOCUS && E.doc >= 0 && !E.committing) {
        MdEdit_Commit(TRUE);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
static BOOL OverlayEnsure(void) {
    if (E.hed) return TRUE;
    E.hed = CreateWindowExW(0, L"Scintilla", NULL,
                            WS_CHILD | WS_VISIBLE | WS_BORDER,
                            0, 0, 0, 0, g_hwndMain, NULL, g_hInst, NULL);
    if (!E.hed) return FALSE;
    SetWindowSubclass(E.hed, OverlaySubclassProc, 1, 0);
    MdEdit_Restyle();
    return TRUE;
}
static void BeginCommon(int doc, const RECT* rcm, int start, int end, BOOL isNew) {
    if (doc < 0 || doc >= g_docCount || !g_docs[doc].hwndEdit) return;
    if (!OverlayEnsure()) return;
    if (E.doc >= 0) MdEdit_Commit(TRUE);
    E.doc = doc;
    E.start = start;
    E.end = end;
    E.isNew = isNew;
    E.committing = FALSE;
    TakeSnapshot();
    int eol = g_docs[doc].eol;
    SendMessage(E.hed, SCI_SETEOLMODE,
                eol == 0 ? SC_EOL_CRLF : (eol == 1 ? SC_EOL_LF : SC_EOL_CR), 0);
    if (E.snapLen > 0) {
        char* tmp = (char*)malloc((size_t)E.snapLen + 1);
        if (tmp) {
            memcpy(tmp, E.snap, (size_t)E.snapLen);
            tmp[E.snapLen] = '\0';
            SendMessage(E.hed, SCI_SETTEXT, 0, (LPARAM)tmp);
            free(tmp);
        }
    } else {
        SendMessage(E.hed, SCI_CLEARALL, 0, 0);
    }
    SendMessage(E.hed, SCI_EMPTYUNDOBUFFER, 0, 0);
    SendMessage(E.hed, SCI_GOTOPOS, 0, 0);
    SetWindowPos(E.hed, HWND_TOP, rcm->left, rcm->top,
                 rcm->right - rcm->left, rcm->bottom - rcm->top,
                 SWP_SHOWWINDOW);
    SetFocus(E.hed);
}
void MdEdit_BeginRange(int doc, int start, int end, const RECT* rcMain) {
    BeginCommon(doc, rcMain, start, end, FALSE);
}
void MdEdit_BeginNewRange(int doc, const RECT* rcMain) {
    int len = (int)SendMessage(g_docs[doc].hwndEdit, SCI_GETLENGTH, 0, 0);
    BeginCommon(doc, rcMain, len, len, TRUE);
}
static BOOL SnapshotStillValid(void) {
    Doc* d = &g_docs[E.doc];
    int curLen = (int)SendMessage(d->hwndEdit, SCI_GETLENGTH, 0, 0);
    if (E.end > curLen) return FALSE;
    if (E.snapLen <= 0) return TRUE;
    char* cur = (char*)malloc((size_t)E.snapLen);
    if (!cur) return FALSE;
    struct Sci_TextRange tr;
    tr.chrg.cpMin = E.start;
    tr.chrg.cpMax = E.end;
    tr.lpstrText = cur;
    SendMessage(d->hwndEdit, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    BOOL ok = memcmp(cur, E.snap, (size_t)E.snapLen) == 0;
    free(cur);
    return ok;
}
void MdEdit_Commit(BOOL apply) {
    if (E.doc < 0 || E.committing) return;
    E.committing = TRUE;
    Doc* d = &g_docs[E.doc];
    HWND main = d->hwndEdit;
    if (apply && main) {
        LRESULT olen = SendMessage(E.hed, SCI_GETLENGTH, 0, 0);
        char* txt = (olen > 0) ? (char*)malloc((size_t)olen + 1) : NULL;
        if (txt && SnapshotStillValid()) {
            struct Sci_TextRange tr;
            tr.chrg.cpMin = 0;
            tr.chrg.cpMax = (Sci_PositionCR)olen;
            tr.lpstrText = txt;
            SendMessage(E.hed, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
            const char* eol = DocEolSeq();
            int eolLen = (int)strlen(eol);
            char* fin = (char*)malloc((size_t)olen + (size_t)eolLen * 3 + 2);
            if (fin) {
                size_t fl = 0;
                if (E.isNew) {
                    int dlen = (int)SendMessage(main, SCI_GETLENGTH, 0, 0);
                    if (dlen > 0) {
                        char tail[3] = { 0, 0, 0 };
                        int from = dlen >= 2 ? dlen - 2 : dlen - 1;
                        struct Sci_TextRange tt;
                        tt.chrg.cpMin = from;
                        tt.chrg.cpMax = dlen;
                        tt.lpstrText = tail;
                        SendMessage(main, SCI_GETTEXTRANGE, 0, (LPARAM)&tt);
                        int tl = dlen - from;
                        BOOL nl = (tail[tl-1] == '\n' || tail[tl-1] == '\r');
                        BOOL dbl = (tl >= 2 && tail[tl-2] == '\n' && tail[tl-1] == '\n');
                        if (!nl) { memcpy(fin + fl, eol, eolLen); fl += eolLen;
                                   memcpy(fin + fl, eol, eolLen); fl += eolLen; }
                        else if (!dbl) { memcpy(fin + fl, eol, eolLen); fl += eolLen; }
                    }
                }
                if (olen > 0) {
                    memcpy(fin + fl, txt, (size_t)olen);
                    fl += (size_t)olen;
                    if (E.snapLen > 0 && E.snap[E.snapLen-1] == '\n' && fin[fl-1] != '\n') {
                        memcpy(fin + fl, eol, eolLen);
                        fl += eolLen;
                    }
                }
                SendMessage(main, SCI_SETTARGETRANGE, E.start, E.end);
                SendMessage(main, SCI_REPLACETARGET, (WPARAM)fl, (LPARAM)fin);
                free(fin);
            }
        }
        free(txt);
    }
    free(E.snap);
    E.snap = NULL;
    E.snapLen = 0;
    E.doc = -1;
    if (GetFocus() == E.hed) SetFocus(MdView_Hwnd());
    ShowWindow(E.hed, SW_HIDE);
    E.committing = FALSE;
}
void MdEdit_OnDocChanging(int index) {
    if (E.doc < 0) return;
    if (E.doc != index) MdEdit_Commit(TRUE);
}
void MdEdit_Restyle(void) {
    if (!E.hed) return;
    Editor_ApplyOverlayStyle(E.hed);
}
BOOL MdEdit_Active(void) {
    return E.doc >= 0;
}
