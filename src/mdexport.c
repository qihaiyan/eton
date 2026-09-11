
#include "common.h"
#include "md4c-html.h"
#include <commdlg.h>

typedef struct { char* p; size_t len, cap; } StrBuf;

static void SbReserve(StrBuf* b, size_t need) {
    if (b->len + need + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->len + need + 1) nc *= 2;
    char* q = (char*)realloc(b->p, nc);
    if (q) { b->p = q; b->cap = nc; }
}
static void SbPut(StrBuf* b, const char* s, size_t n) {
    SbReserve(b, n);
    if (!b->p) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}
static void SbStr(StrBuf* b, const char* s) { SbPut(b, s, strlen(s)); }

static void MdHtmlOut(const MD_CHAR* s, MD_SIZE n, void* ud) {
    SbPut((StrBuf*)ud, s, n);
}

static void MathDelimit(StrBuf* in) {
    static const char openD[] = "<x-equation type=\"display\">";
    static const char openI[] = "<x-equation>";
    static const char closeT[] = "</x-equation>";
    StrBuf out = { 0 };
    size_t i = 0;
    int depth = 0;
    int dispStack[32]; int dsp = 0;
    while (i < in->len) {
        if (i + sizeof(openD) - 1 <= in->len &&
            memcmp(in->p + i, openD, sizeof(openD) - 1) == 0) {
            SbStr(&out, "\\[");
            if (dsp < 32) dispStack[dsp++] = 1;
            i += sizeof(openD) - 1;
            continue;
        }
        if (i + sizeof(openI) - 1 <= in->len &&
            memcmp(in->p + i, openI, sizeof(openI) - 1) == 0) {
            SbStr(&out, "\\(");
            if (dsp < 32) dispStack[dsp++] = 0;
            i += sizeof(openI) - 1;
            continue;
        }
        if (i + sizeof(closeT) - 1 <= in->len &&
            memcmp(in->p + i, closeT, sizeof(closeT) - 1) == 0) {
            int disp = dsp > 0 ? dispStack[--dsp] : 0;
            SbStr(&out, disp ? "\\]" : "\\)");
            i += sizeof(closeT) - 1;
            continue;
        }
        SbPut(&out, in->p + i, 1);
        i++;
    }
    free(in->p);
    *in = out;
    (void)depth;
}

static void MermaidBlocks(StrBuf* in) {
    static const char pat[] = "<pre><code class=\"language-mermaid\">";
    static const char end[] = "</code></pre>";
    static const char repO[] = "<pre class=\"mermaid\">";
    static const char repC[] = "</pre>";
    StrBuf out = { 0 };
    size_t i = 0;
    while (i < in->len) {
        if (i + sizeof(pat) - 1 <= in->len &&
            memcmp(in->p + i, pat, sizeof(pat) - 1) == 0) {
            size_t e = i + sizeof(pat) - 1;
            size_t found = (size_t)-1;
            for (size_t j = e; j + sizeof(end) - 1 <= in->len; j++) {
                if (memcmp(in->p + j, end, sizeof(end) - 1) == 0) { found = j; break; }
            }
            if (found != (size_t)-1) {
                SbStr(&out, repO);
                SbPut(&out, in->p + e, found - e);
                SbStr(&out, repC);
                i = found + sizeof(end) - 1;
                continue;
            }
        }
        SbPut(&out, in->p + i, 1);
        i++;
    }
    free(in->p);
    *in = out;
}

static const char* kHtmlCss =
"body{font-family:'Segoe UI','Microsoft YaHei',sans-serif;max-width:820px;"
"margin:24px auto;padding:0 16px;color:#1f2328;line-height:1.6}"
"h1,h2{border-bottom:1px solid #d0d7de;padding-bottom:6px}"
"code{background:#f6f8fa;padding:2px 5px;border-radius:4px;font-family:Consolas,monospace}"
"pre{background:#f6f8fa;padding:12px;border-radius:8px;overflow-x:auto}"
"pre code{background:none;padding:0}"
"blockquote{border-left:4px solid #d0d7de;margin:8px 0;padding:2px 14px;color:#57606a}"
"table{border-collapse:collapse}td,th{border:1px solid #d0d7de;padding:6px 10px}"
"th{background:#ecf0f3}img{max-width:100%}";

static void AppendMermaidScript(StrBuf* html, BOOL* usedRes) {
    HRSRC hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(IDR_MERMAID_JS), RT_RCDATA);
    if (hr) {
        HGLOBAL hg = LoadResource(g_hInst, hr);
        const char* js = (const char*)LockResource(hg);
        DWORD sz = SizeofResource(g_hInst, hr);
        if (js && sz) {
            SbStr(html, "\n<script>");
            SbPut(html, js, sz);
            SbStr(html, "</script>\n<script>mermaid.initialize({startOnLoad:true});</script>");
            *usedRes = TRUE;
            return;
        }
    }
    SbStr(html, "\n<script src=\"https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js\">"
                "</script>\n<script>mermaid.initialize({startOnLoad:true});</script>");
    *usedRes = FALSE;
}

void MdExport_Html(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    if (g_docs[g_curDoc].lang != LANG_MD) return;

    DWORD len = 0;
    char* utf8 = Editor_GetTextUtf8(g_curDoc, &len);
    if (!utf8) return;

    StrBuf body = { 0 };
    md_html(utf8, (MD_SIZE)len, MdHtmlOut, &body,
            MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS, 0);
    free(utf8);
    MathDelimit(&body);
    MermaidBlocks(&body);

    wchar_t fname[MAX_PATH];
    wcscpy_s(fname, MAX_PATH, g_docs[g_curDoc].title);
    wchar_t* dot = wcsrchr(fname, L'.');
    if (dot) *dot = 0;
    wcscat_s(fname, MAX_PATH, L".html");
    wchar_t filter[128];
    I18n_JoinFilter(filter, 128, STR_FILTER_ALL, STR_FILTER_ALLPAT, 0, 0);
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFilter = L"HTML (*.html)\0*.html\0\0";
    ofn.lpstrFile = fname;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) { free(body.p); return; }

    StrBuf html = { 0 };
    SbStr(&html, "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
    SbStr(&html, "<title>Document</title>\n");
    SbStr(&html, "<style>"); SbStr(&html, kHtmlCss); SbStr(&html, "</style>\n");
    SbStr(&html, "</head>\n<body>\n");
    if (body.p) SbPut(&html, body.p, body.len);
    free(body.p);
    BOOL usedRes = FALSE;
    AppendMermaidScript(&html, &usedRes);
    SbStr(&html, "\n<script src=\"https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js\"></script>\n");
    SbStr(&html, "</body>\n</html>\n");

    HANDLE f = CreateFileW(fname, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(f, html.p ? html.p : "", (DWORD)(html.len ? html.len : 0), &written, NULL);
        CloseHandle(f);
    } else {
        MessageBoxW(g_hwndMain, T(STR_MD_MERMAID_UNSUP), L"eton", MB_ICONERROR);
    }
    free(html.p);
    (void)usedRes; (void)filter;
}

void MdExport_Pdf(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    if (g_docs[g_curDoc].lang != LANG_MD) return;

    PRINTDLGW pd; ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = g_hwndMain;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION;
    if (!PrintDlgW(&pd) || !pd.hDC) return;

    int pw = GetDeviceCaps(pd.hDC, HORZRES);
    int ph = GetDeviceCaps(pd.hDC, VERTRES);
    DOCINFOW di; ZeroMemory(&di, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = g_docs[g_curDoc].title;

    if (StartDocW(pd.hDC, &di) > 0) {
        MdView_PrintPages(pd.hDC, pw, ph);
        EndDoc(pd.hDC);
    }
    DeleteDC(pd.hDC);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
}
