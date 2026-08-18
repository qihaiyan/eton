#include "common.h"
#define FD_MAX_LINES 200000
#define FD_MAX_D 1000
#define FD_CTX 3
enum { FD_MARK_HUNK = 24, FD_MARK_DEL = 25, FD_MARK_ADD = 26 };
typedef struct { const char* p; int len; } FDLine;
typedef struct { char op; int a, b; } FDOp;
static struct {
    FDLine* a; int na;
    FDLine* b; int nb;
    FDOp* ops; int nOps, capOps;
    char* out; size_t outLen, outCap;
    int outLine;
    int* mkLine; unsigned char* mkKind; int nMk, capMk;
} D;
static void Mk(int line, int kind) {
    if (D.nMk >= D.capMk) {
        int nc = D.capMk ? D.capMk * 2 : 4096;
        int* nl = (int*)realloc(D.mkLine, (size_t)nc * sizeof(int));
        unsigned char* nk = (unsigned char*)realloc(D.mkKind, (size_t)nc);
        if (!nl || !nk) { free(nl); free(nk); return; }
        D.mkLine = nl;
        D.mkKind = nk;
        D.capMk = nc;
    }
    D.mkLine[D.nMk] = line;
    D.mkKind[D.nMk] = (unsigned char)kind;
    D.nMk++;
}
static void OpPush(char op, int a, int b) {
    if (D.nOps >= D.capOps) {
        int nc = D.capOps ? D.capOps * 2 : 4096;
        FDOp* no = (FDOp*)realloc(D.ops, (size_t)nc * sizeof(FDOp));
        if (!no) return;
        D.ops = no;
        D.capOps = nc;
    }
    D.ops[D.nOps].op = op;
    D.ops[D.nOps].a = a;
    D.ops[D.nOps].b = b;
    D.nOps++;
}
static void Out(const char* s, size_t len) {
    if (D.outLen + len + 1 > D.outCap) {
        size_t nc = D.outCap ? D.outCap * 2 : 65536;
        while (nc < D.outLen + len + 1) nc *= 2;
        char* no = (char*)realloc(D.out, nc);
        if (!no) return;
        D.out = no;
        D.outCap = nc;
    }
    memcpy(D.out + D.outLen, s, len);
    D.outLen += len;
    D.out[D.outLen] = '\0';
}
static void OutLine(char prefix, const char* p, int len, int mark) {
    char pre[2] = { prefix, 0 };
    Out(pre, 1);
    Out(p, (size_t)len);
    Out("\n", 1);
    if (mark >= 0) Mk(D.outLine, mark);
    D.outLine++;
}
static int SplitLines(char* txt, size_t len, FDLine** outLines) {
    FDLine* L = (FDLine*)malloc(sizeof(FDLine) * (FD_MAX_LINES + 1));
    if (!L) { *outLines = NULL; return 0; }
    int n = 0;
    char* p = txt;
    char* end = txt + len;
    while (p < end && n < FD_MAX_LINES) {
        char* e = p;
        while (e < end && *e != '\n') e++;
        int l = (int)(e - p);
        if (l > 0 && e[-1] == '\r') l--;
        L[n].p = p;
        L[n].len = l;
        n++;
        p = (e < end) ? e + 1 : e;
    }
    *outLines = L;
    return n;
}
static int LineEq(const FDLine* x, const FDLine* y) {
    return x->len == y->len && (x->len == 0 || memcmp(x->p, y->p, (size_t)x->len) == 0);
}
static BOOL Myers(void) {
    int n = D.na, m = D.nb;
    int off = FD_MAX_D;
    int width = 2 * FD_MAX_D + 1;
    int* v = (int*)calloc((size_t)width, sizeof(int));
    int** trace = (int**)calloc((size_t)FD_MAX_D + 1, sizeof(int*));
    if (!v || !trace) { free(v); free(trace); return FALSE; }
    BOOL found = FALSE;
    int foundD = -1;
    for (int d = 0; d <= FD_MAX_D && !found; d++) {
        trace[d] = (int*)malloc(sizeof(int) * (size_t)width);
        if (!trace[d]) break;
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[k - 1 + off] < v[k + 1 + off]))
                x = v[k + 1 + off];
            else
                x = v[k - 1 + off] + 1;
            int y = x - k;
            while (x < n && y < m && LineEq(&D.a[x], &D.b[y])) { x++; y++; }
            v[k + off] = x;
            if (x >= n && y >= m) {
                memcpy(trace[d], v, sizeof(int) * (size_t)width);
                found = TRUE;
                foundD = d;
                break;
            }
        }
        if (!found) memcpy(trace[d], v, sizeof(int) * (size_t)width);
    }
    if (found) {
        int x = n, y = m;
        for (int d = foundD; d > 0; d--) {
            int k = x - y;
            int pk = (k == -d || (k != d && trace[d - 1][k - 1 + off] < trace[d - 1][k + 1 + off]))
                     ? k + 1 : k - 1;
            int px = trace[d - 1][pk + off];
            int py = px - pk;
            while (x > px && y > py) { OpPush('=', x - 1, y - 1); x--; y--; }
            if (x == px) { OpPush('+', x, py); y = py; }
            else { OpPush('-', px, y); x = px; }
        }
        while (x > 0 && y > 0) { OpPush('=', x - 1, y - 1); x--; y--; }
        for (int i = 0; i < D.nOps / 2; i++) {
            FDOp t = D.ops[i];
            D.ops[i] = D.ops[D.nOps - 1 - i];
            D.ops[D.nOps - 1 - i] = t;
        }
    }
    for (int d = 0; d <= FD_MAX_D; d++) free(trace[d]);
    free(trace);
    free(v);
    return found;
}
static void EmitHunk(int s, int e) {
    int aLen = 0, bLen = 0;
    for (int i = s; i <= e; i++) {
        if (D.ops[i].op != '+') aLen++;
        if (D.ops[i].op != '-') bLen++;
    }
    int aStart = D.ops[s].a;
    int bStart = D.ops[s].b;
    char hdr[96];
    int nh = sprintf_s(hdr, sizeof hdr, "@@ -%d,%d +%d,%d @@\n",
                       aStart + 1, aLen, bStart + 1, bLen);
    Out(hdr, (size_t)nh);
    Mk(D.outLine, FD_MARK_HUNK);
    D.outLine++;
    for (int i = s; i <= e; i++) {
        const FDOp* o = &D.ops[i];
        if (o->op == '=') OutLine(' ', D.a[o->a].p, D.a[o->a].len, -1);
        else if (o->op == '-') OutLine('-', D.a[o->a].p, D.a[o->a].len, FD_MARK_DEL);
        else OutLine('+', D.b[o->b].p, D.b[o->b].len, FD_MARK_ADD);
    }
}
static void Emit(void) {
    int i = 0;
    while (i < D.nOps) {
        if (D.ops[i].op != '=') {
            int s = i, e = i;
            int j = i;
            while (j < D.nOps) {
                if (D.ops[j].op != '=') { e = j; j++; }
                else {
                    int gap = 0;
                    int k = j;
                    while (k < D.nOps && D.ops[k].op == '=') { k++; gap++; }
                    if (gap <= FD_CTX * 2 && k < D.nOps) { j = k; }
                    else break;
                }
            }
            i = e + 1;
            int cs = s, ce = e;
            for (int c = 0; cs > 0 && D.ops[cs - 1].op == '=' && c < FD_CTX; c++) cs--;
            for (int c = 0; ce + 1 < D.nOps && D.ops[ce + 1].op == '=' && c < FD_CTX; c++) ce++;
            EmitHunk(cs, ce);
        } else {
            i++;
        }
    }
}
static char* ReadSide(const wchar_t* path, DWORD* outLen) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > 64 * 1024 * 1024) { CloseHandle(h); return NULL; }
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    if (size) ReadFile(h, buf, size, &rd, NULL);
    CloseHandle(h);
    buf[rd] = '\0';
    if (rd >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
        int wlen = (int)((rd - 2) / 2);
        int u8 = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)(buf + 2), wlen, NULL, 0, NULL, NULL);
        if (u8 > 0) {
            char* cv = (char*)malloc((size_t)u8 + 1);
            if (cv) {
                WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)(buf + 2), wlen, cv, u8, NULL, NULL);
                cv[u8] = '\0';
                free(buf);
                *outLen = (DWORD)u8;
                return cv;
            }
        }
        free(buf);
        return NULL;
    }
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, rd - 3);
        rd -= 3;
        buf[rd] = '\0';
    }
    *outLen = rd;
    return buf;
}
void FileDiff_Run(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    wchar_t nameA[MAX_PATH] = L"";
    if (g_docs[g_curDoc].path[0]) {
        const wchar_t* bs = wcsrchr(g_docs[g_curDoc].path, L'\\');
        wcscpy_s(nameA, MAX_PATH, bs ? bs + 1 : g_docs[g_curDoc].path);
    } else {
        wcscpy_s(nameA, MAX_PATH, g_docs[g_curDoc].baseTitle);
    }
    wchar_t filter[256];
    I18n_JoinFilter(filter, 256, STR_FILTER_ALL, STR_FILTER_ALLPAT,
                             STR_FILTER_ALL, STR_FILTER_ALLPAT);
    wchar_t fileB[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileB;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    wchar_t nameB[MAX_PATH];
    const wchar_t* bs2 = wcsrchr(fileB, L'\\');
    wcscpy_s(nameB, MAX_PATH, bs2 ? bs2 + 1 : fileB);
    DWORD lenA = 0;
    char* txtA = Editor_GetTextUtf8(g_curDoc, &lenA);
    DWORD lenB = 0;
    char* txtB = ReadSide(fileB, &lenB);
    if (!txtB) {
        if (txtA) free(txtA);
        MessageBoxW(g_hwndMain, T(STR_MSG_DIFF_BIG), T(STR_APP_TITLE), MB_OK | MB_ICONWARNING);
        return;
    }
    if ((int)lenA > FD_MAX_LINES * 4096 || lenB > FD_MAX_LINES * 4096) {
        free(txtA); free(txtB);
        MessageBoxW(g_hwndMain, T(STR_MSG_DIFF_BIG), T(STR_APP_TITLE), MB_OK | MB_ICONWARNING);
        return;
    }
    D.na = SplitLines(txtA, lenA, &D.a);
    D.nb = SplitLines(txtB, lenB, &D.b);
    if (D.na >= FD_MAX_LINES || D.nb >= FD_MAX_LINES) {
        free(txtA); free(txtB); free(D.a); free(D.b);
        D.a = D.b = NULL;
        MessageBoxW(g_hwndMain, T(STR_MSG_DIFF_BIG), T(STR_APP_TITLE), MB_OK | MB_ICONWARNING);
        return;
    }
    D.nOps = 0;
    D.outLen = 0;
    D.outLine = 0;
    D.nMk = 0;
    if (D.out) D.out[0] = '\0';
    BOOL ok = Myers();
    if (!ok) {
        D.nOps = 0;
        for (int i = 0; i < D.na; i++) OpPush('-', i, 0);
        for (int j = 0; j < D.nb; j++) OpPush('+', 0, j);
    }
    BOOL any = FALSE;
    for (int i = 0; i < D.nOps; i++)
        if (D.ops[i].op != '=') { any = TRUE; break; }
    if (!any) {
        free(txtA); free(txtB); free(D.a); free(D.b);
        D.a = D.b = NULL;
        MessageBoxW(g_hwndMain, T(STR_MSG_DIFF_SAME), T(STR_APP_TITLE), MB_OK | MB_ICONINFORMATION);
        return;
    }
    char hdrA[600], hdrB[600];
    int nhA = sprintf_s(hdrA, sizeof hdrA, "--- %ls\n", nameA);
    int nhB = sprintf_s(hdrB, sizeof hdrB, "+++ %ls\n", nameB);
    Out(hdrA, (size_t)nhA);
    Out(hdrB, (size_t)nhB);
    D.outLine = 2;
    Emit();
    wchar_t title[MAX_PATH * 2];
    WFmt(title, L"%s vs %s", nameA, nameB);
    int ni = Editor_NewDoc(NULL, title, FALSE);
    if (ni < 0) {
        free(txtA); free(txtB); free(D.a); free(D.b);
        D.a = D.b = NULL;
        return;
    }
    Editor_Activate(ni);
    Editor_SetText(ni, D.out ? D.out : "");
    Editor_MarkDirty(ni, FALSE);
    HWND hed = g_docs[ni].hwndEdit;
    SendMessage(hed, SCI_MARKERDEFINE, FD_MARK_DEL, SC_MARK_BACKGROUND);
    SendMessage(hed, SCI_MARKERSETBACK, FD_MARK_DEL,
                (LPARAM)(g_dark ? RGB(96,44,48) : RGB(255,224,224)));
    SendMessage(hed, SCI_MARKERDEFINE, FD_MARK_ADD, SC_MARK_BACKGROUND);
    SendMessage(hed, SCI_MARKERSETBACK, FD_MARK_ADD,
                (LPARAM)(g_dark ? RGB(38,80,52) : RGB(224,255,224)));
    for (int i = 0; i < D.nMk; i++)
        SendMessage(hed, SCI_MARKERADD, (WPARAM)D.mkLine[i], D.mkKind[i]);
    free(txtA); free(txtB); free(D.a); free(D.b);
    D.a = D.b = NULL;
}
