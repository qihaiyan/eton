#include "common.h"

/* ---------- internal state ---------- */
static int g_tabHeight = 26;
static int g_statusHeight = 22;

/* Scintilla lexer names indexed by LangID (1-based in our enum, 0=NONE) */
static const char* LexerName[] = {
    "",       /* LANG_NONE */
    "cpp",    /* LANG_C  (cpp lexer covers C) */
    "cpp",    /* LANG_CPP */
    "cpp",    /* LANG_CS (cpp lexer + cs keywords) */
    "cpp",    /* LANG_JAVA (cpp lexer + java keywords) */
    "cpp",    /* LANG_JS (cpp lexer + js keywords) */
    "python", /* LANG_PY */
    "xml",    /* LANG_XML */
    "json",   /* LANG_JSON */
    "sql"     /* LANG_SQL */
};

/* ---------- colors ---------- */
void Editor_ApplyThemeColors(void) {
    if (g_dark) {
        g_clrBg       = RGB(30,30,30);
        g_clrFg       = RGB(220,220,220);
        g_clrSelBg    = RGB(60,60,85);
        g_clrGutterBg = RGB(40,40,40);
        g_clrGutterFg = RGB(150,150,150);
        g_clrStatusBg = RGB(45,45,48);
        g_clrStatusFg = RGB(220,220,220);
    } else {
        g_clrBg       = RGB(255,255,255);
        g_clrFg       = RGB(0,0,0);
        g_clrSelBg    = RGB(180,200,235);
        g_clrGutterBg = RGB(240,240,240);
        g_clrGutterFg = RGB(110,110,110);
        g_clrStatusBg = RGB(240,240,240);
        g_clrStatusFg = RGB(0,0,0);
    }
}

/* ---------- font ---------- */
void Editor_SetFont(void) {
    if (g_hFont) DeleteObject(g_hFont);
    HDC hdc = GetDC(NULL);
    int h = -MulDiv(g_fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    g_hFont = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    /* Apply to all Scintilla editors via SCI_STYLESETFONT (expects UTF-8 name) */
    for (int i = 0; i < g_docCount; i++) {
        if (g_docs[i].hwndEdit) {
            SendMessage(g_docs[i].hwndEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, g_fontSize);
            SendMessage(g_docs[i].hwndEdit, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
            SendMessage(g_docs[i].hwndEdit, SCI_STYLECLEARALL, 0, 0);
        }
    }
    Editor_Layout();
}

/* ---------- init ---------- */
BOOL Editor_Init(void) {
    /* Register Scintilla window class */
    Scintilla_RegisterClasses(g_hInst);

    TabBar_Register();
    Editor_ApplyThemeColors();
    g_fontSize = 11;
    Editor_SetFont();
    return TRUE;
}
static void ApplyScintillaStyle(HWND hed) {
    /* Default style */
    SendMessage(hed, SCI_STYLESETBACK, STYLE_DEFAULT, (LPARAM)g_clrBg);
    SendMessage(hed, SCI_STYLESETFORE, STYLE_DEFAULT, (LPARAM)g_clrFg);
    SendMessage(hed, SCI_STYLESETSIZE, STYLE_DEFAULT, g_fontSize);
    SendMessage(hed, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
    SendMessage(hed, SCI_STYLECLEARALL, 0, 0);

    /* Line number margin (margin 0) */
    SendMessage(hed, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    SendMessage(hed, SCI_STYLESETBACK, STYLE_LINENUMBER, (LPARAM)g_clrGutterBg);
    SendMessage(hed, SCI_STYLESETFORE, STYLE_LINENUMBER, (LPARAM)g_clrGutterFg);

    /* Selection colors */
    SendMessage(hed, SCI_SETSELBACK, 1, (LPARAM)g_clrSelBg);
    SendMessage(hed, SCI_SETSELFORE, 1, (LPARAM)g_clrFg);

    /* Caret */
    SendMessage(hed, SCI_SETCARETFORE, (LPARAM)g_clrFg, 0);
    SendMessage(hed, SCI_SETCARETWIDTH, 1, 0);

    /* Tab settings */
    SendMessage(hed, SCI_SETUSETABS, 0, 0);       /* use spaces */
    SendMessage(hed, SCI_SETTABWIDTH, 4, 0);

    /* EOL visibility off */
    SendMessage(hed, SCI_SETVIEWEOL, 0, 0);

    /* Disable horizontal scrollbar when wrapping */
    SendMessage(hed, SCI_SETHSCROLLBAR, g_wordWrap ? 0 : 1, 0);

    /* Apply dark/light syntax colors if a language is set */
    Editor_ComputeGutterWidth(Editor_IndexFromHwnd(hed));
}

/* ---------- apply language lexer + keywords ---------- */
static void ApplyLexer(HWND hed, LangID lang) {
    if (lang == LANG_NONE) {
        /* Clear lexer: set to null lexer */
        ILexer5* nullLex = CreateLexer("null");
        if (nullLex) {
            SendMessage(hed, SCI_SETILEXER, 0, (LPARAM)nullLex);
        }
        return;
    }
    const char* name = LexerName[lang];
    ILexer5* lex = CreateLexer(name);
    if (!lex) return;
    SendMessage(hed, SCI_SETILEXER, 0, (LPARAM)lex);

    /* For cpp-family lexers, set keywords per language.
       Scintilla cpp lexer keyword sets: 0=primary, 1=secondary, 2=doc comment.
       We set different keyword lists based on the exact language. */
    if (lang == LANG_C || lang == LANG_CPP || lang == LANG_CS ||
        lang == LANG_JAVA || lang == LANG_JS) {
        const char* kw = "auto break case char const continue default do double else enum extern float for goto if int long register return short signed sizeof static struct switch typedef union unsigned void volatile while bool class delete friend inline namespace new operator private protected public this throw try catch virtual template typename using override final abstract async await extends implements import package static var let const function true false null undefined";
        SendMessage(hed, SCI_SETKEYWORDS, 0, (LPARAM)kw);
    } else if (lang == LANG_PY) {
        const char* kw = "and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield None True False self cls print int str list dict tuple float bool bytes object type super enumerate range len open map filter sorted zip reversed iter next hasattr getattr setattr isinstance issubclass ord chr abs max min sum round pow divmod hash id dir vars repr format input";
        SendMessage(hed, SCI_SETKEYWORDS, 0, (LPARAM)kw);
    } else if (lang == LANG_SQL) {
        const char* kw = "select from where insert update delete create table drop alter add index view database into values set join inner left right outer on group by order having limit offset distinct as and or not null primary key foreign references begin commit rollback transaction case when then else end procedure function trigger cursor fetch open close declare execute if exists between like in is asc desc union all cross natural using with recursive over partition window rank row_number";
        SendMessage(hed, SCI_SETKEYWORDS, 0, (LPARAM)kw);
    } else if (lang == LANG_JSON) {
        const char* kw = "true false null";
        SendMessage(hed, SCI_SETKEYWORDS, 0, (LPARAM)kw);
    }
    /* XML/HTML lexer doesn't need keywords */

    /* Set syntax colors based on theme */
    /* Scintilla style IDs for cpp lexer:
       SCE_C_DEFAULT=0, COMMENT=1/2/3, NUMBER=4, KEYWORD=5, STRING=6, PREPROC=9, etc. */
    COLORREF c_comment, c_string, c_number, c_keyword, c_preproc, c_operator;
    if (g_dark) {
        c_comment  = RGB(106,153,85);
        c_string   = RGB(206,145,120);
        c_number   = RGB(181,206,168);
        c_keyword  = RGB(86,156,214);
        c_preproc  = RGB(197,134,192);
        c_operator = RGB(180,180,180);
    } else {
        c_comment  = RGB(0,128,0);
        c_string   = RGB(163,21,21);
        c_number   = RGB(9,105,192);
        c_keyword  = RGB(0,0,255);
        c_preproc  = RGB(155,33,200);
        c_operator = RGB(120,120,120);
    }
    /* Common style IDs across our lexers */
    SendMessage(hed, SCI_STYLESETFORE, 1, (LPARAM)c_comment);   /* comment line */
    SendMessage(hed, SCI_STYLESETFORE, 2, (LPARAM)c_comment);   /* comment block */
    SendMessage(hed, SCI_STYLESETFORE, 3, (LPARAM)c_comment);   /* comment doc */
    SendMessage(hed, SCI_STYLESETFORE, 4, (LPARAM)c_number);
    SendMessage(hed, SCI_STYLESETFORE, 5, (LPARAM)c_keyword);
    SendMessage(hed, SCI_STYLESETFORE, 6, (LPARAM)c_string);
    SendMessage(hed, SCI_STYLESETFORE, 7, (LPARAM)c_string);    /* char */
    SendMessage(hed, SCI_STYLESETFORE, 9, (LPARAM)c_preproc);
    SendMessage(hed, SCI_STYLESETFORE, 10, (LPARAM)c_operator);
    SendMessage(hed, SCI_STYLESETBOLD, 5, 1);                   /* bold keywords */
}

/* ---------- helpers ---------- */
void Editor_UpdateTitle(int index) {
    Doc* d = &g_docs[index];
    /* Derive the clean base title from baseTitle (for new files) or from
       the file path (for opened files). Never copy from d->title itself --
       it may already have a " *" dirty suffix, and re-appending " *" on
       every keystroke would make the title grow unboundedly until it
       overflows d->title[256] and crashes. */
    wchar_t base[256];
    if (d->isNew) {
        wcscpy_s(base, 256, d->baseTitle);
    } else {
        wchar_t nm[MAX_PATH];
        wcscpy_s(nm, MAX_PATH, d->path);
        PathStripPathW(nm);
        wcscpy_s(base, 256, nm);
    }
    if (d->dirty) wsprintf(d->title, L"%s *", base);
    else wcscpy_s(d->title, 256, base);
}

void Editor_MarkDirty(int index, BOOL dirty) {
    g_docs[index].dirty = dirty;
    Editor_UpdateTitle(index);
    InvalidateRect(g_hwndTab, NULL, FALSE);
}

/* ---------- create doc ---------- */
int Editor_NewDoc(const wchar_t* path, const wchar_t* title, BOOL isNew) {
    if (g_docCount >= MAX_DOCS) return -1;
    int idx = g_docCount;
    Doc* d = &g_docs[idx];

    d->hwndEdit = CreateWindowExW(0, L"Scintilla", NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, g_hwndMain, NULL, g_hInst, NULL);
    if (!d->hwndEdit) { MessageBoxW(g_hwndMain, L"创建 Scintilla 控件失败。", L"错误", MB_ICONERROR); return -1; }

    /* Configure Scintilla */
    SendMessage(d->hwndEdit, SCI_SETCODEPAGE, SC_CP_UTF8, 0);  /* UTF-8 internal */
    SendMessage(d->hwndEdit, SCI_SETEOLMODE, SC_EOL_LF, 0);
    SendMessage(d->hwndEdit, SCI_SETCARETWIDTH, 1, 0);
    SendMessage(d->hwndEdit, SCI_EMPTYUNDOBUFFER, 0, 0);
    SendMessage(d->hwndEdit, SCI_SETSAVEPOINT, 0, 0);  /* mark as unmodified */
    ApplyScintillaStyle(d->hwndEdit);

    d->isNew = isNew;
    d->dirty = FALSE;
    d->eol = 1;   /* 默认 Unix (LF) 行尾 */
    d->draft[0] = L'\0';
    d->lang = LANG_NONE;
    if (isNew) {
        d->path[0] = L'\0';
        wcscpy_s(d->baseTitle, 256, title ? title : L"未命名");
        d->enc = ENC_UTF8;
    } else {
        wcscpy_s(d->path, MAX_PATH, path); d->path[MAX_PATH-1]=L'\0';
        d->enc = ENC_UTF8;
        /* baseTitle for opened files is derived from path in Editor_UpdateTitle */
        d->baseTitle[0] = L'\0';
    }

    g_docCount++;
    Editor_UpdateTitle(idx);
    return idx;
}

/* ---------- load file into doc ---------- */
/* 整体替换文档内容（不产生脏标记/撤销历史），加载文件与恢复草稿共用 */
void Editor_SetText(int index, const char* utf8) {
    HWND hed = g_docs[index].hwndEdit;
    g_suppressDirty = TRUE;
    SendMessage(hed, WM_SETREDRAW, FALSE, 0);
    SendMessage(hed, SCI_SETTEXT, 0, (LPARAM)utf8);
    SendMessage(hed, SCI_EMPTYUNDOBUFFER, 0, 0);
    SendMessage(hed, SCI_SETSAVEPOINT, 0, 0);
    SendMessage(hed, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hed, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    g_suppressDirty = FALSE;
}

BOOL Editor_LoadFile(int index, const wchar_t* path, Encoding enc) {
    Encoding detected = ENC_UTF8;
    int eol = 0;
    char* utf8 = LoadFileToUtf8(path, &detected, &eol);
    if (!utf8) return FALSE;
    Doc* d = &g_docs[index];
    Editor_SetText(index, utf8);
    /* EOL mode */
    int sci_eol = (eol == 0) ? SC_EOL_CRLF : (eol == 1 ? SC_EOL_LF : SC_EOL_CR);
    SendMessage(d->hwndEdit, SCI_SETEOLMODE, sci_eol, 0);
    /* Goto start */
    SendMessage(d->hwndEdit, SCI_GOTOPOS, 0, 0);
    d->eol = eol;
    d->enc = detected;
    d->isNew = FALSE;
    wcscpy_s(d->path, MAX_PATH, path);
    d->dirty = FALSE;
    free(utf8);
    Editor_UpdateTitle(index);
    return TRUE;
}

/* ---------- activate / layout ---------- */
void Editor_Activate(int index) {
    if (index < 0 || index >= g_docCount) return;
    g_curDoc = index;
    for (int i = 0; i < g_docCount; i++) {
        BOOL show = (i == index);
        ShowWindow(g_docs[i].hwndEdit, show ? SW_SHOW : SW_HIDE);
    }
    Editor_ApplyWrap(index);
    Editor_Layout();
    InvalidateRect(g_hwndTab, NULL, FALSE);
    Editor_UpdateStatus();
    Session_Save();   /* 标签集/激活标签变化时落盘，异常退出也不丢会话 */
}

void Editor_ApplyWrap(int index) {
    if (index < 0 || index >= g_docCount) return;
    HWND hed = g_docs[index].hwndEdit;
    /* Scintilla wrap mode - no need for style hack like RichEdit */
    SendMessage(hed, SCI_SETWRAPMODE, g_wordWrap ? SC_WRAP_WORD : SC_WRAP_NONE, 0);
    SendMessage(hed, SCI_SETHSCROLLBAR, g_wordWrap ? 0 : 1, 0);
    Editor_Layout();
}

int Editor_IndexFromHwnd(HWND hed) {
    for (int i = 0; i < g_docCount; i++)
        if (g_docs[i].hwndEdit == hed) return i;
    return -1;
}

void Editor_ConvertEol(int index, int eol) {
    if (index < 0 || index >= g_docCount) return;
    HWND hed = g_docs[index].hwndEdit;
    int sci_eol = (eol == 0) ? SC_EOL_CRLF : (eol == 1 ? SC_EOL_LF : SC_EOL_CR);
    SendMessage(hed, SCI_SETEOLMODE, sci_eol, 0);
    SendMessage(hed, SCI_CONVERTEOLS, sci_eol, 0);
    g_docs[index].eol = eol;
    Editor_MarkDirty(index, TRUE);
    Editor_UpdateStatus();
}

void Editor_ComputeGutterWidth(int index) {
    if (index < 0 || index >= g_docCount) return;
    HWND hed = g_docs[index].hwndEdit;
    int lines = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
    int digits = 3;
    int t = lines; while (t >= 10) { digits++; t /= 10; }
    /* Scintilla measures in pixels; ~8px per digit for Consolas 11pt */
    int w = digits * 8 + 6;
    if (w < 36) w = 36;
    g_gutterWidth = w;
    SendMessage(hed, SCI_SETMARGINWIDTHN, 0, w);  /* margin 0 = line numbers */
}

void Editor_Layout(void) {
    RECT rc; GetClientRect(g_hwndMain, &rc);
    int cx = rc.right, cy = rc.bottom;

    SetWindowPos(g_hwndTab, NULL, 0, 0, cx, g_tabHeight, SWP_NOZORDER);
    SetWindowPos(g_hwndStatus, NULL, 0, cy - g_statusHeight, cx, g_statusHeight, SWP_NOZORDER);

    int top = g_tabHeight;
    int bottom = cy - g_statusHeight;
    int h = bottom - top;

    if (g_curDoc >= 0 && g_curDoc < g_docCount) {
        Editor_ComputeGutterWidth(g_curDoc);
        /* Scintilla owns its own line-number margin, so the edit fills the whole area */
        SetWindowPos(g_docs[g_curDoc].hwndEdit, NULL, 0, top, cx, h, SWP_NOZORDER);
    }
}

/* ---------- zoom / wrap / lang / theme ---------- */
void Editor_Zoom(int delta) {
    g_fontSize += delta;
    if (g_fontSize < 6) g_fontSize = 6;
    if (g_fontSize > 48) g_fontSize = 48;
    /* Scintilla has its own zoom; but we keep our font size model */
    for (int i = 0; i < g_docCount; i++) {
        if (g_docs[i].hwndEdit) {
            SendMessage(g_docs[i].hwndEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, g_fontSize);
            SendMessage(g_docs[i].hwndEdit, SCI_STYLECLEARALL, 0, 0);
        }
    }
    Editor_UpdateStatus();
}

void Editor_SetLang(int index, LangID lang) {
    g_docs[index].lang = lang;
    ApplyLexer(g_docs[index].hwndEdit, lang);
}

void Editor_ApplyTheme(int index) {
    ApplyScintillaStyle(g_docs[index].hwndEdit);
    if (g_docs[index].lang != LANG_NONE)
        ApplyLexer(g_docs[index].hwndEdit, g_docs[index].lang);
}

/* ---------- gutter update ---------- */
void Editor_UpdateGutter(int index) {
    if (index < 0 || index >= g_docCount) return;
    Editor_ComputeGutterWidth(index);
}

/* ---------- notification handling ---------- */
void Editor_OnNotify(LPARAM lp) {
    SCNotification* scn = (SCNotification*)lp;
    HWND hed = (HWND)scn->nmhdr.hwndFrom;
    int idx = Editor_IndexFromHwnd(hed);
    if (idx < 0) return;

    if (scn->nmhdr.code == SCN_UPDATEUI) {
        Editor_UpdateStatus();
    } else if (scn->nmhdr.code == SCN_MODIFIED) {
        if (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
            if (!g_suppressDirty) {
                Editor_MarkDirty(idx, TRUE);
                Editor_UpdateStatus();
                Editor_UpdateGutter(idx);
                if (g_docs[idx].isNew) g_draftsDirty = TRUE;   /* 未命名文档内容变化，待写草稿 */
            }
        }
    }
}

/* ---------- text access (UTF-8) ---------- */
static char* Scintilla_GetTextUtf8(HWND hed, DWORD* lenOut) {
    DWORD len = (DWORD)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    char* buf = (char*)malloc(len + 1);
    if (!buf) { *lenOut = 0; return NULL; }
    SendMessage(hed, SCI_GETTEXT, len + 1, (LPARAM)buf);
    *lenOut = len;
    return buf;
}

char* Editor_GetTextUtf8(int index, DWORD* outLen) {
    if (index < 0 || index >= g_docCount) { *outLen = 0; return NULL; }
    return Scintilla_GetTextUtf8(g_docs[index].hwndEdit, outLen);
}

/* ---------- save ---------- */
BOOL Editor_SaveDoc(int index, BOOL askPath) {
    Doc* d = &g_docs[index];
    if (askPath || d->isNew || d->path[0] == L'\0') {
        return Editor_SaveDocAs(index);
    }
    DWORD len = 0;
    char* text = Scintilla_GetTextUtf8(d->hwndEdit, &len);
    if (!text) return FALSE;
    BOOL ok = SaveUtf8ToFile(d->path, text, len, d->enc, d->eol);
    free(text);
    if (ok) {
        d->dirty = FALSE;
        SendMessage(d->hwndEdit, SCI_SETSAVEPOINT, 0, 0);
        AddRecent(d->path);
        Editor_UpdateTitle(index);
        InvalidateRect(g_hwndTab, NULL, FALSE);
    } else {
        MessageBoxW(g_hwndMain, L"保存失败。", L"错误", MB_ICONERROR);
    }
    return ok;
}

BOOL Editor_SaveDocAs(int index) {
    Doc* d = &g_docs[index];
    wchar_t fname[MAX_PATH] = {0};
    if (!d->isNew) { wcscpy_s(fname, MAX_PATH, d->path); PathStripPathW(fname); }
    else wcscpy_s(fname, MAX_PATH, L"未命名.txt");

    wchar_t filter[] = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fname;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return FALSE;

    wcscpy_s(d->path, MAX_PATH, fname);
    d->isNew = FALSE;
    d->baseTitle[0] = L'\0';  /* now derived from path in Editor_UpdateTitle */
    DWORD len = 0;
    char* text = Scintilla_GetTextUtf8(d->hwndEdit, &len);
    if (!text) return FALSE;
    BOOL ok = SaveUtf8ToFile(d->path, text, len, d->enc, d->eol);
    free(text);
    if (ok) {
        d->dirty = FALSE;
        SendMessage(d->hwndEdit, SCI_SETSAVEPOINT, 0, 0);
        AddRecent(d->path);
        Editor_UpdateTitle(index);
        InvalidateRect(g_hwndTab, NULL, FALSE);
        Session_DiscardDraft(index);   /* 另存为正式文件后，移除其草稿 */
        Session_SaveDrafts();   /* 未命名文档集合变化 */
    }
    return ok;
}

/* ---------- close ---------- */
void Editor_CloseDoc(int index) {
    if (index < 0 || index >= g_docCount) return;
    /* 草稿（未命名文档）不弹"未保存"提示：关闭标签即弃稿；异常退出由草稿兜底 */
    if (!g_docs[index].isNew && g_docs[index].dirty) {
        wchar_t msg[300];
        wsprintf(msg, L"文件 \"%s\" 已修改，是否保存后再关闭？", g_docs[index].title);
        int r = MessageBoxW(g_hwndMain, msg, L"eton", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) { if (!Editor_SaveDoc(index, FALSE)) return; }
    } else if (g_docs[index].isNew) {
        Session_DiscardDraft(index);
    }
    DestroyWindow(g_docs[index].hwndEdit);
    for (int i = index; i < g_docCount - 1; i++) g_docs[i] = g_docs[i + 1];
    g_docCount--;
    if (g_docCount == 0) {
        int ni = Editor_NewDoc(NULL, L"未命名", TRUE);
        Editor_Activate(ni);
    } else {
        if (index < g_curDoc) g_curDoc--;   /* 关闭的是活动标签左侧的标签时，活动索引随之前移 */
        if (g_curDoc >= g_docCount) g_curDoc = g_docCount - 1;
        if (g_curDoc >= 0) Editor_Activate(g_curDoc);
    }
    Session_SaveDrafts();   /* 未命名文档集合变化，立即同步草稿（关闭即弃稿） */
}

int Editor_FindDocByPath(const wchar_t* path) {
    for (int i = 0; i < g_docCount; i++) {
        if (!g_docs[i].isNew && _wcsicmp(g_docs[i].path, path) == 0) return i;
    }
    return -1;
}

/* 按路径打开文件：已打开则切换到对应标签，否则新建标签载入 */
void OpenFileByPath(const wchar_t* path) {
    int ex = Editor_FindDocByPath(path);
    if (ex >= 0) { Editor_Activate(ex); return; }
    int ni = Editor_NewDoc(path, NULL, FALSE);
    if (ni < 0) return;
    if (!Editor_LoadFile(ni, path, ENC_UTF8)) { Editor_CloseDoc(ni); return; }
    AddRecent(path);
    Editor_Activate(ni);
}

/* ---------- status ---------- */
HWND Editor_ActiveEdit(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return NULL;
    return g_docs[g_curDoc].hwndEdit;
}

void Editor_UpdateStatus(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) {
        SendMessage(g_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"");
        return;
    }
    HWND hed = g_docs[g_curDoc].hwndEdit;
    Doc* d = &g_docs[g_curDoc];
    Sci_Position cp = (Sci_Position)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position selStart = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONEND, 0, 0);
    int line = (int)SendMessage(hed, SCI_LINEFROMPOSITION, cp, 0);
    int lineStart = (int)SendMessage(hed, SCI_POSITIONFROMLINE, line, 0);
    int col = (int)(cp - lineStart) + 1;
    int totalLines = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
    Sci_Position len = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);

    wchar_t s1[64], s2[32], s3[32], s4[64], s5[64];
    if (selStart != selEnd)
        wsprintf(s1, L"已选 %d 个字符", (int)(selEnd - selStart));
    else
        wsprintf(s1, L"共 %d 个字符", (int)len);
    wsprintf(s2, L"第 %d 行 / 共 %d 行", line + 1, totalLines);
    wsprintf(s3, L"列 %d", col);
    wsprintf(s4, L"%s", EncodingName(d->enc));
    const wchar_t* eolname = d->eol == 0 ? L"CRLF" : (d->eol == 1 ? L"LF" : L"CR");
    wsprintf(s5, L"%s  |  %d%%", eolname, g_fontSize * 10);
    SendMessage(g_hwndStatus, SB_SETTEXT, 0, (LPARAM)s1);
    SendMessage(g_hwndStatus, SB_SETTEXT, 1, (LPARAM)s2);
    SendMessage(g_hwndStatus, SB_SETTEXT, 2, (LPARAM)s3);
    SendMessage(g_hwndStatus, SB_SETTEXT, 3, (LPARAM)s4);
    SendMessage(g_hwndStatus, SB_SETTEXT, 4, (LPARAM)s5);
}
