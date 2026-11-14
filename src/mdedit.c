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
typedef struct {
    const char* s;
    int len;
    MdSrcBlock* v;
    int n, cap;
    int run;
    BOOL runQuote;
    int runList;
    BOOL prevText;
    int fence;
    char fenceChar;
} ScanSt;
static BOOL ScanFlush(ScanSt* c, int end) {
    if (c->run < 0) return TRUE;
    int e = end;
    while (e > c->run && (c->s[e-1] == '\n' || c->s[e-1] == '\r')) e--;
    if (e < end) e++;
    if (e > c->len) e = c->len;
    c->v = BlkPush(c->v, &c->n, &c->cap, c->run, e);
    c->run = -1;
    c->runQuote = FALSE;
    c->runList = -1;
    c->prevText = FALSE;
    return c->v != NULL;
}
static int ListMarkerType(const char* s, int p, int ce) {
    char c0 = s[p];
    if (c0 == '-' || c0 == '+' || c0 == '*') {
        if (p + 1 < ce && (s[p+1] == ' ' || s[p+1] == '\t')) return 0;
        return -1;
    }
    int q = p;
    while (q < ce && s[q] >= '0' && s[q] <= '9') q++;
    if (q > p && q - p <= 9 && q < ce && (s[q] == '.' || s[q] == ')') &&
        (q + 1 >= ce || s[q+1] == ' ' || s[q+1] == '\t'))
        return 1;
    return -1;
}
static BOOL BlankContinuesList(ScanSt* c, int le) {
    if (c->runList < 0) return FALSE;
    int q = le + 1;
    while (q < c->len) {
        int qe = q;
        while (qe < c->len && c->s[qe] != '\n') qe++;
        int qce = qe;
        while (qce > q && c->s[qce-1] == '\r') qce--;
        int qp = q;
        while (qp < qce && (c->s[qp] == ' ' || c->s[qp] == '\t')) qp++;
        if (qp < qce) {
            int qind = qp - q;
            if (qind >= 2) return TRUE;
            return ListMarkerType(c->s, qp, qce) == c->runList;
        }
        if (qe >= c->len) break;
        q = qe + 1;
    }
    return FALSE;
}
int MdEditScanBlocks(const char* s, int len, MdSrcBlock** out) {
    *out = NULL;
    if (!s || len <= 0) return 0;
    ScanSt c;
    ZeroMemory(&c, sizeof c);
    c.s = s;
    c.len = len;
    c.run = -1;
    c.fence = -1;
    c.runList = -1;
    int ls = 0;
    while (ls < len) {
        int le = ls;
        while (le < len && s[le] != '\n') le++;
        int ce = le;
        while (ce > ls && s[ce-1] == '\r') ce--;
        int p = ls;
        while (p < ce && (s[p] == ' ' || s[p] == '\t')) p++;
        int indent = p - ls;
        BOOL blank = (p >= ce);
        int endnl = (le < len) ? le + 1 : len;
        if (c.fence >= 0) {
            if (!blank && s[p] == c.fenceChar) {
                int q = p;
                while (q < ce && s[q] == c.fenceChar) q++;
                if (q - p >= 3) {
                    c.v = BlkPush(c.v, &c.n, &c.cap, c.fence, endnl);
                    if (!c.v) return 0;
                    c.fence = -1;
                    c.run = -1;
                    c.runQuote = FALSE;
                    c.runList = -1;
                    c.prevText = FALSE;
                }
            }
        } else if (blank) {
            if (!BlankContinuesList(&c, le)) {
                if (!ScanFlush(&c, ls)) return 0;
            }
        } else {
            char c0 = s[p];
            BOOL isFence = FALSE;
            BOOL isHeading = FALSE;
            if ((c0 == '`' || c0 == '~') && indent <= 3) {
                int q = p;
                while (q < ce && s[q] == c0) q++;
                if (q - p >= 3 && (q >= ce || s[q] == ' ' || s[q] == '\t'))
                    isFence = TRUE;
            } else if (c0 == '#' && indent <= 3) {
                int q = p;
                while (q < ce && s[q] == '#') q++;
                if (q - p <= 6 && (q >= ce || s[q] == ' ' || s[q] == '\t'))
                    isHeading = TRUE;
            }
            char hr = 0;
            if (!isFence && !isHeading) {
                char m = 0;
                int cnt = 0;
                BOOL only = TRUE;
                int q = p;
                while (q < ce) {
                    char ch = s[q];
                    if (ch == ' ' || ch == '\t') { q++; continue; }
                    if ((ch == '-' || ch == '*' || ch == '_' || ch == '=') &&
                        (m == 0 || ch == m)) {
                        if (m == 0) m = ch;
                        cnt++;
                        q++;
                        continue;
                    }
                    only = FALSE;
                    break;
                }
                if (only && m &&
                    ((m == '=' && cnt >= 1) ||
                     ((m == '-' || m == '*' || m == '_') && cnt >= 3)))
                    hr = m;
            }
            if (isFence) {
                if (!ScanFlush(&c, ls)) return 0;
                c.fence = ls;
                c.fenceChar = c0;
            } else if (isHeading) {
                if (!ScanFlush(&c, ls)) return 0;
                c.v = BlkPush(c.v, &c.n, &c.cap, ls, endnl);
                if (!c.v) return 0;
                c.prevText = FALSE;
            } else if ((hr == '-' || hr == '=') && c.run >= 0 && c.prevText) {
                c.v = BlkPush(c.v, &c.n, &c.cap, c.run, endnl);
                if (!c.v) return 0;
                c.run = -1;
                c.runQuote = FALSE;
                c.runList = -1;
                c.prevText = FALSE;
            } else if (hr) {
                if (!ScanFlush(&c, ls)) return 0;
                c.v = BlkPush(c.v, &c.n, &c.cap, ls, endnl);
                if (!c.v) return 0;
                c.prevText = FALSE;
            } else {
                BOOL isQuote = (c0 == '>' && indent <= 3);
                int lt = -1;
                if (!isQuote && indent <= 3)
                    lt = ListMarkerType(s, p, ce);
                if (isQuote) {
                    if (!(c.run >= 0 && c.runQuote)) {
                        if (!ScanFlush(&c, ls)) return 0;
                        c.run = ls;
                        c.runQuote = TRUE;
                        c.runList = -1;
                    }
                    c.prevText = TRUE;
                } else if (lt >= 0) {
                    if (!(c.run >= 0 && c.runList == lt)) {
                        if (!ScanFlush(&c, ls)) return 0;
                        c.run = ls;
                        c.runQuote = FALSE;
                        c.runList = lt;
                    }
                    c.prevText = TRUE;
                } else {
                    if (c.run < 0) {
                        c.run = ls;
                        c.runQuote = FALSE;
                        c.runList = -1;
                    }
                    c.prevText = TRUE;
                }
            }
        }
        ls = le + 1;
    }
    if (c.fence >= 0) {
        c.v = BlkPush(c.v, &c.n, &c.cap, c.fence, len);
        if (!c.v) return 0;
    } else if (c.run >= 0) {
        int e = len;
        while (e > c.run && (s[e-1] == '\n' || s[e-1] == '\r')) e--;
        if (e < len) e++;
        c.v = BlkPush(c.v, &c.n, &c.cap, c.run, e);
        if (!c.v) return 0;
    }
    *out = c.v;
    return c.v ? c.n : 0;
}

static struct {
    HWND hed;
    int doc;
    int blk;
    int start, end;
    char* snap;
    int snapLen;
    BOOL isNew;
    BOOL committing;
    BOOL dirty;
    BOOL loading;
} E = { NULL, -1, -1, 0, 0, NULL, 0, FALSE, FALSE, FALSE, FALSE };

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
static BOOL RescanRange(int blk, int* start, int* end) {
    if (blk < 0 || blk >= MdView_BlockCount()) return FALSE;
    DWORD len = 0;
    char* u8 = Editor_GetTextUtf8(E.doc, &len);
    if (!u8) return FALSE;
    MdSrcBlock* bs = NULL;
    int n = MdEditScanBlocks(u8, (int)len, &bs);
    BOOL ok = (n == MdView_BlockCount() && blk < n);
    if (ok) { *start = bs[blk].start; *end = bs[blk].end; }
    free(bs);
    free(u8);
    return ok;
}
static BOOL ApplyCommit(void) {
    if (!E.hed || E.doc < 0 || E.doc >= g_docCount) return FALSE;
    Doc* d = &g_docs[E.doc];
    HWND main = d->hwndEdit;
    if (!main) return FALSE;
    if (!SnapshotStillValid()) return FALSE;
    LRESULT olen = SendMessage(E.hed, SCI_GETLENGTH, 0, 0);
    char* txt = (char*)malloc((size_t)olen + 1);
    if (!txt) return FALSE;
    if (olen > 0) {
        struct Sci_TextRange tr;
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = (Sci_PositionCR)olen;
        tr.lpstrText = txt;
        SendMessage(E.hed, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    }
    const char* eol = DocEolSeq();
    int eolLen = (int)strlen(eol);
    char* fin = (char*)malloc((size_t)olen + (size_t)eolLen * 3 + 2);
    if (!fin) { free(txt); return FALSE; }
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
    free(txt);
    E.dirty = FALSE;
    return TRUE;
}
static void SessionEnd(BOOL hideOverlay) {
    free(E.snap);
    E.snap = NULL;
    E.snapLen = 0;
    E.doc = -1;
    E.blk = -1;
    E.dirty = FALSE;
    E.loading = FALSE;
    KillTimer(g_hwndMain, 4);
    if (hideOverlay && E.hed) {
        if (GetFocus() == E.hed) SetFocus(MdView_Hwnd());
        ShowWindow(E.hed, SW_HIDE);
    }
}
void MdEdit_Commit(BOOL apply) {
    if (E.doc < 0 || E.committing) return;
    E.committing = TRUE;
    if (apply) ApplyCommit();
    E.committing = FALSE;
    SessionEnd(TRUE);
}
static void CommitQuiet(void) {
    if (E.doc < 0 || E.committing || E.blk < 0) return;
    E.committing = TRUE;
    BOOL ok = ApplyCommit();
    E.committing = FALSE;
    if (!ok) { MdEdit_Commit(FALSE); return; }
    MdView_SoftRefresh();
    KillTimer(g_hwndMain, 3);
    int s, t;
    if (!RescanRange(E.blk, &s, &t)) { MdEdit_Commit(FALSE); return; }
    E.start = s;
    E.end = t;
    E.isNew = FALSE;
    TakeSnapshot();
    RECT rcm;
    if (!MdView_BlockRect(E.blk, &rcm)) { MdEdit_Commit(FALSE); return; }
    RECT old;
    GetWindowRect(E.hed, &old);
    MapWindowPoints(NULL, GetParent(E.hed), (POINT*)&old, 2);
    if (old.left != rcm.left || old.top != rcm.top ||
        old.right != rcm.right || old.bottom != rcm.bottom)
        SetWindowPos(E.hed, MdView_Hwnd(), rcm.left, rcm.top,
                     rcm.right - rcm.left, rcm.bottom - rcm.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    MdView_EnsureOverlayVisible(&rcm);
}
void MdEdit_Tick(void) {
    if (E.doc < 0 || !E.dirty || !E.hed) return;
    CommitQuiet();
}
BOOL MdEdit_OnNotify(LPARAM lp) {
    if (!E.hed) return FALSE;
    SCNotification* scn = (SCNotification*)lp;
    if (scn->nmhdr.hwndFrom != E.hed) return FALSE;
    if (!E.loading && scn->nmhdr.code == SCN_MODIFIED &&
        (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        E.dirty = TRUE;
        SetTimer(g_hwndMain, 4, 300, NULL);
    }
    return TRUE;
}
static void MergePrev(int doc, int blk);
static LRESULT CALLBACK OverlaySubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR id, DWORD_PTR ref) {
    if (E.doc >= 0 && msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
            MdEdit_Commit(TRUE);
            return 0;
        }
        LRESULT pos = SendMessage(E.hed, SCI_GETCURRENTPOS, 0, 0);
        LRESULT line = SendMessage(E.hed, SCI_LINEFROMPOSITION, pos, 0);
        LRESULT nLines = SendMessage(E.hed, SCI_GETLINECOUNT, 0, 0);
        if (wp == VK_UP && line == 0 && E.blk > 0) {
            int doc = E.doc, blk = E.blk;
            MdEdit_Commit(TRUE);
            MdView_SoftRefresh();
            MdEdit_OpenAt(doc, blk - 1, 0x7FFFFFFF, TRUE, -1);
            return 0;
        }
        if (wp == VK_DOWN && line == nLines - 1 &&
            E.blk >= 0 && E.blk + 1 < MdView_BlockCount()) {
            int doc = E.doc, blk = E.blk;
            MdEdit_Commit(TRUE);
            MdView_SoftRefresh();
            MdEdit_OpenAt(doc, blk + 1, 0, FALSE, -1);
            return 0;
        }
        if (wp == VK_RETURN && pos == SendMessage(E.hed, SCI_GETLENGTH, 0, 0) &&
            E.blk >= 0) {
            int doc = E.doc, blk = E.blk;
            RECT rcm;
            BOOL have = MdView_BlockRect(blk, &rcm);
            MdEdit_Commit(TRUE);
            MdView_SoftRefresh();
            if (have) {
                int lineH = UI_Scale(28);
                rcm.top = rcm.bottom + UI_Scale(6);
                rcm.bottom = rcm.top + lineH;
                MdView_EnsureOverlayVisible(&rcm);
                RECT rcv;
                GetClientRect(MdView_Hwnd(), &rcv);
                RECT rcm2;
                GetWindowRect(MdView_Hwnd(), &rcm2);
                POINT org = { rcm2.left, rcm2.top };
                ScreenToClient(GetParent(MdView_Hwnd()), &org);
                OffsetRect(&rcv, org.x, org.y);
                IntersectRect(&rcm, &rcm, &rcv);
                if (rcm.bottom > rcm.top + UI_Scale(12))
                    MdEdit_BeginNewRange(doc, &rcm);
            }
            return 0;
        }
        if (wp == VK_BACK && pos == 0 &&
            SendMessage(E.hed, SCI_GETSELECTIONEMPTY, 0, 0) &&
            E.blk > 0) {
            int doc = E.doc, blk = E.blk;
            MdEdit_Commit(TRUE);
            MdView_SoftRefresh();
            MergePrev(doc, blk);
            return 0;
        }
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
static int Utf8CharCount(const char* s, int len) {
    int n = 0;
    for (int i = 0; i < len; ) {
        unsigned char c = (unsigned char)s[i];
        i += (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : 4;
        n++;
    }
    return n;
}
static void MergePrev(int doc, int blk) {
    DWORD len = 0;
    char* u8 = Editor_GetTextUtf8(doc, &len);
    if (!u8) return;
    MdSrcBlock* bs = NULL;
    int n = MdEditScanBlocks(u8, (int)len, &bs);
    if (n == MdView_BlockCount() && blk < n && blk >= 1) {
        MdSrcBlock pv = bs[blk-1];
        MdSrcBlock cu = bs[blk];
        int a = (cu.start > pv.end) ? pv.end : pv.end - 1;
        if (a >= pv.start && cu.start >= a) {
            int chars = Utf8CharCount(u8 + pv.start, a - pv.start);
            HWND main = g_docs[doc].hwndEdit;
            SendMessage(main, SCI_SETTARGETRANGE, a, cu.start);
            SendMessage(main, SCI_REPLACETARGET, 0, (LPARAM)"");
            MdView_SoftRefresh();
            MdEdit_OpenAt(doc, blk - 1, 0, FALSE, chars);
        }
    }
    free(bs);
    free(u8);
}
static void BeginCommon(int doc, const RECT* rcm, int start, int end,
                        BOOL isNew, int blk, int caretLine, BOOL caretEnd, int caretChar) {
    if (doc < 0 || doc >= g_docCount || !g_docs[doc].hwndEdit) return;
    if (!OverlayEnsure()) return;
    if (E.doc >= 0) MdEdit_Commit(TRUE);
    E.doc = doc;
    E.blk = blk;
    E.start = start;
    E.end = end;
    E.isNew = isNew;
    E.committing = FALSE;
    E.dirty = FALSE;
    E.loading = TRUE;
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
    LRESULT nLines = SendMessage(E.hed, SCI_GETLINECOUNT, 0, 0);
    int cl = caretLine;
    if (cl >= nLines) cl = (int)nLines - 1;
    if (cl < 0) cl = 0;
    SendMessage(E.hed, SCI_GOTOLINE, cl, 0);
    if (caretChar >= 0) {
        LRESULT docLen = SendMessage(E.hed, SCI_GETLENGTH, 0, 0);
        LRESULT p = caretChar;
        if (p > docLen) p = docLen;
        SendMessage(E.hed, SCI_GOTOPOS, p, 0);
    } else if (caretEnd) {
        SendMessage(E.hed, SCI_LINEEND, 0, 0);
    } else {
        SendMessage(E.hed, SCI_HOME, 0, 0);
    }
    SetWindowPos(E.hed, MdView_Hwnd(), rcm->left, rcm->top,
                 rcm->right - rcm->left, rcm->bottom - rcm->top,
                 SWP_SHOWWINDOW);
    E.loading = FALSE;
    E.dirty = FALSE;
    KillTimer(g_hwndMain, 4);
    SetFocus(E.hed);
}
BOOL MdEdit_OpenAt(int doc, int blkIdx, int caretLine, BOOL caretEnd, int caretChar) {
    if (doc < 0 || doc >= g_docCount) return FALSE;
    if (MdEdit_Active()) return FALSE;
    int s, t;
    int saveDoc = E.doc;
    E.doc = doc;
    BOOL ok = RescanRange(blkIdx, &s, &t);
    E.doc = saveDoc;
    RECT rcm;
    if (!ok || !MdView_BlockRect(blkIdx, &rcm)) {
        StatusBar_SetText(0, T(STR_MD_NOBLOCK));
        return FALSE;
    }
    BeginCommon(doc, &rcm, s, t, FALSE, blkIdx, caretLine, caretEnd, caretChar);
    MdView_EnsureOverlayVisible(&rcm);
    return TRUE;
}
void MdEdit_BeginNewRange(int doc, const RECT* rcMain) {
    int len = (int)SendMessage(g_docs[doc].hwndEdit, SCI_GETLENGTH, 0, 0);
    BeginCommon(doc, rcMain, len, len, TRUE, -1, 0, FALSE, -1);
    MdView_EnsureOverlayVisible(rcMain);
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
