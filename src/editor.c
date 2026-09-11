#include "common.h"

static const int TAB_HEIGHT_96 = 26;
static const int STATUS_HEIGHT_96 = 22;

static const char* LexerName[] = {
    "",
    "cpp",
    "cpp",
    "cpp",
    "cpp",
    "cpp",
    "python",
    "xml",
    "json",
    "sql",
    "markdown"
};

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

static void RecreateTabFont(void) {
    if (g_hFont) DeleteObject(g_hFont);
    int h = -MulDiv(g_fontSize, (int)g_dpi, 72);
    g_hFont = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

void Editor_SetFont(void) {
    RecreateTabFont();
    for (int i = 0; i < g_docCount; i++) {
        if (g_docs[i].hwndEdit) {
            SendMessage(g_docs[i].hwndEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, g_fontSize);
            SendMessage(g_docs[i].hwndEdit, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
            SendMessage(g_docs[i].hwndEdit, SCI_STYLECLEARALL, 0, 0);
        }
    }
    Editor_Layout();
}

void Editor_OnDpiChanged(void) {
    RecreateTabFont();
    Editor_Layout();
    InvalidateRect(g_hwndTab, NULL, TRUE);
}

BOOL Editor_Init(void) {
    Scintilla_RegisterClasses(g_hInst);

    TabBar_Register();
    MdView_Register();
    MdView_Create(g_hwndMain);
    Editor_ApplyThemeColors();
    Editor_SetFont();
    return TRUE;
}
static void ApplyScintillaStyle(HWND hed) {
    SendMessage(hed, SCI_STYLESETBACK, STYLE_DEFAULT, (LPARAM)g_clrBg);
    SendMessage(hed, SCI_STYLESETFORE, STYLE_DEFAULT, (LPARAM)g_clrFg);
    SendMessage(hed, SCI_STYLESETSIZE, STYLE_DEFAULT, g_fontSize);
    SendMessage(hed, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
    SendMessage(hed, SCI_STYLECLEARALL, 0, 0);

    SendMessage(hed, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    SendMessage(hed, SCI_STYLESETBACK, STYLE_LINENUMBER, (LPARAM)g_clrGutterBg);
    SendMessage(hed, SCI_STYLESETFORE, STYLE_LINENUMBER, (LPARAM)g_clrGutterFg);

    SendMessage(hed, SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    SendMessage(hed, SCI_SETMARGINMASKN, 1, SC_MASK_FOLDERS);
    SendMessage(hed, SCI_SETMARGINSENSITIVEN, 1, 1);
    SendMessage(hed, SCI_SETFOLDMARGINCOLOUR, 1, (LPARAM)g_clrGutterBg);
    SendMessage(hed, SCI_SETFOLDMARGINHICOLOUR, 1, (LPARAM)g_clrGutterBg);
    {
        COLORREF fFold = g_dark ? RGB(220,220,220) : RGB(80,80,80);
        COLORREF bFold = g_dark ? RGB(60,70,90)    : RGB(200,200,200);
        struct { int num; int mark; } fm[] = {
            { SC_MARKNUM_FOLDEROPEN,    SC_MARK_BOXMINUS },
            { SC_MARKNUM_FOLDER,        SC_MARK_BOXPLUS },
            { SC_MARKNUM_FOLDERSUB,     SC_MARK_VLINE },
            { SC_MARKNUM_FOLDERTAIL,    SC_MARK_TCORNER },
            { SC_MARKNUM_FOLDEREND,     SC_MARK_BOXPLUSCONNECTED },
            { SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED },
            { SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER },
        };
        for (int i = 0; i < (int)(sizeof(fm)/sizeof(fm[0])); i++) {
            SendMessage(hed, SCI_MARKERDEFINE, fm[i].num, fm[i].mark);
            SendMessage(hed, SCI_MARKERSETFORE, fm[i].num, (LPARAM)fFold);
            SendMessage(hed, SCI_MARKERSETBACK, fm[i].num, (LPARAM)bFold);
        }
    }

    SendMessage(hed, SCI_MARKERDEFINE, 24, SC_MARK_BACKGROUND);
    SendMessage(hed, SCI_MARKERSETFORE, 24, (LPARAM)(g_dark ? RGB(80,70,25) : RGB(255,238,170)));

    SendMessage(hed, SCI_INDICSETSTYLE, 20, INDIC_ROUNDBOX);
    SendMessage(hed, SCI_INDICSETFORE, 20, (LPARAM)(g_dark ? RGB(255,220,100) : RGB(255,200,0)));
    SendMessage(hed, SCI_INDICSETALPHA, 20, 50);

    SendMessage(hed, SCI_SETCARETLINEVISIBLE, 1, 0);
    SendMessage(hed, SCI_SETCARETLINEBACK, (LPARAM)(g_dark ? RGB(40,40,46) : RGB(242,242,234)), 0);

    SendMessage(hed, SCI_SETSELBACK, 1, (LPARAM)g_clrSelBg);
    SendMessage(hed, SCI_SETSELFORE, 1, (LPARAM)g_clrFg);

    SendMessage(hed, SCI_SETCARETFORE, (LPARAM)g_clrFg, 0);
    SendMessage(hed, SCI_SETCARETWIDTH, 1, 0);

    SendMessage(hed, SCI_SETUSETABS, 0, 0);
    SendMessage(hed, SCI_SETTABWIDTH, 4, 0);

    SendMessage(hed, SCI_SETVIEWEOL, 0, 0);

    SendMessage(hed, SCI_USEPOPUP, 0, 0);

    SendMessage(hed, SCI_SETVSCROLLBAR, 0, 0);
    SendMessage(hed, SCI_SETHSCROLLBAR, 0, 0);

    Editor_ComputeGutterWidth(Editor_IndexFromHwnd(hed));
}

static void ApplyLexer(HWND hed, LangID lang) {
    SendMessage(hed, SCI_SETMARGINWIDTHN, 1,
                (lang == LANG_NONE || lang == LANG_MD) ? 0 : UI_Scale(14));
    if (lang == LANG_NONE) {
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

    SendMessage(hed, SCI_SETPROPERTY, (WPARAM)"fold", (LPARAM)"1");
    SendMessage(hed, SCI_SETPROPERTY, (WPARAM)"fold.compact", (LPARAM)"0");

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

    if (lang == LANG_MD) {
        COLORREF cHead, cLink, cQuote, cCode;
        if (g_dark) {
            cHead  = RGB(86,156,214);
            cLink  = RGB(78,201,176);
            cQuote = RGB(106,153,85);
            cCode  = RGB(206,145,120);
        } else {
            cHead  = RGB(9,105,192);
            cLink  = RGB(1,109,163);
            cQuote = RGB(0,128,0);
            cCode  = RGB(163,21,21);
        }
        SendMessage(hed, SCI_STYLESETFORE, 2, (LPARAM)cHead);
        SendMessage(hed, SCI_STYLESETBOLD, 2, 1);
        SendMessage(hed, SCI_STYLESETITALIC, 3, 1);
        for (int s = 4; s <= 9; s++) {
            SendMessage(hed, SCI_STYLESETFORE, s, (LPARAM)cHead);
            SendMessage(hed, SCI_STYLESETBOLD, s, 1);
            SendMessage(hed, SCI_STYLESETSIZE, s, g_fontSize + (9 - s));
        }
        SendMessage(hed, SCI_STYLESETFORE, 13, (LPARAM)cQuote);
        SendMessage(hed, SCI_STYLESETFORE, 14, (LPARAM)g_clrGutterFg);
        SendMessage(hed, SCI_STYLESETFORE, 15, (LPARAM)g_clrGutterFg);
        SendMessage(hed, SCI_STYLESETFORE, 16, (LPARAM)cLink);
        SendMessage(hed, SCI_STYLESETUNDERLINE, 16, 1);
        SendMessage(hed, SCI_STYLESETFORE, 18, (LPARAM)cCode);
        SendMessage(hed, SCI_STYLESETFORE, 11, (LPARAM)cQuote);
        SendMessage(hed, SCI_STYLESETFORE, 12, (LPARAM)cQuote);
        return;
    }

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
    SendMessage(hed, SCI_STYLESETFORE, 1, (LPARAM)c_comment);
    SendMessage(hed, SCI_STYLESETFORE, 2, (LPARAM)c_comment);
    SendMessage(hed, SCI_STYLESETFORE, 3, (LPARAM)c_comment);
    SendMessage(hed, SCI_STYLESETFORE, 4, (LPARAM)c_number);
    SendMessage(hed, SCI_STYLESETFORE, 5, (LPARAM)c_keyword);
    SendMessage(hed, SCI_STYLESETFORE, 6, (LPARAM)c_string);
    SendMessage(hed, SCI_STYLESETFORE, 7, (LPARAM)c_string);
    SendMessage(hed, SCI_STYLESETFORE, 9, (LPARAM)c_preproc);
    SendMessage(hed, SCI_STYLESETFORE, 10, (LPARAM)c_operator);
    SendMessage(hed, SCI_STYLESETBOLD, 5, 1);
}

static LangID LangFromPath(const wchar_t* path) {
    static const struct { const wchar_t* ext; LangID lang; } map[] = {
        { L".c",    LANG_C },  { L".h",    LANG_C },
        { L".cpp",  LANG_CPP },{ L".cc",   LANG_CPP },{ L".cxx", LANG_CPP },
        { L".hpp",  LANG_CPP },{ L".hh",   LANG_CPP },
        { L".cs",   LANG_CS }, { L".java", LANG_JAVA },
        { L".js",   LANG_JS }, { L".mjs",  LANG_JS },{ L".ts", LANG_JS },
        { L".py",   LANG_PY }, { L".pyw",  LANG_PY },
        { L".xml",  LANG_XML },{ L".html", LANG_XML },{ L".htm", LANG_XML },
        { L".xaml", LANG_XML },{ L".svg",  LANG_XML },{ L".xsl", LANG_XML },
        { L".json", LANG_JSON },{ L".sql",  LANG_SQL },
        { L".md",   LANG_MD }, { L".markdown", LANG_MD },
    };
    const wchar_t* ext = PathFindExtensionW(path);
    if (!ext || ext == path) return LANG_NONE;
    for (int i = 0; i < (int)(sizeof(map)/sizeof(map[0])); i++)
        if (_wcsicmp(ext, map[i].ext) == 0) return map[i].lang;
    return LANG_NONE;
}

static BOOL DiskWriteTime(const wchar_t* path, FILETIME* ft) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) {
        ZeroMemory(ft, sizeof(*ft));
        return FALSE;
    }
    *ft = fa.ftLastWriteTime;
    return TRUE;
}

static BOOL ConfirmAnsiOk(HWND hed) {
    char buf[64 * 1024 + 8];
    wchar_t w[64 * 1024];
    char probe[128 * 1024 + 8];
    Sci_Position total = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    Sci_Position pos = 0;
    BOOL bad = FALSE;
    while (pos < total && !bad) {
        Sci_Position take = total - pos;
        if (take > (Sci_Position)(sizeof(buf) - 8)) take = sizeof(buf) - 8;
        struct Sci_TextRangeFull tr;
        tr.chrg.cpMin = pos; tr.chrg.cpMax = pos + take; tr.lpstrText = buf;
        SendMessage(hed, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);
        if (pos + take < total) {
            while (take > 0 && buf[take-1] != '\n') take--;
            if (take == 0) {
                take = (total - pos < (Sci_Position)(sizeof(buf) - 8)) ? total - pos : (Sci_Position)(sizeof(buf) - 8);
                while (take > 0 && ((unsigned char)buf[take-1] & 0xC0) == 0x80) take--;
                if (take > 0 && (unsigned char)buf[take-1] >= 0xC0) take--;
            }
        }
        pos += take;
        int n = (take > 0) ? MultiByteToWideChar(CP_UTF8, 0, buf, (int)take, w, 64 * 1024) : 0;
        if (n > 0) {
            BOOL used = FALSE;
            WideCharToMultiByte(CP_ACP, 0, w, n, probe, sizeof(probe), "?", &used);
            if (used) bad = TRUE;
        }
    }
    if (!bad) return TRUE;
    int r = MessageBoxW(g_hwndMain, T(STR_MSG_ANSI_WARN), T(STR_TITLE_SAVE),
                        MB_YESNO | MB_ICONWARNING);
    return r == IDYES;
}

static BOOL ConfirmOverwriteOk(Doc* d) {
    if (d->isNew || d->path[0] == L'\0') return TRUE;
    FILETIME cur;
    if (!DiskWriteTime(d->path, &cur)) return TRUE;
    if (CompareFileTime(&cur, &d->ftWrite) == 0) return TRUE;
    wchar_t msg[400];
    wsprintf(msg, T(STR_MSG_FILE_CHANGED_OVERWRITE), d->title);
    return MessageBoxW(g_hwndMain, msg, T(STR_TITLE_SAVE),
                       MB_YESNO | MB_ICONWARNING) == IDYES;
}

void Editor_UpdateTitle(int index) {
    Doc* d = &g_docs[index];
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

int Editor_NewDoc(const wchar_t* path, const wchar_t* title, BOOL isNew) {
    if (g_docCount >= MAX_DOCS) return -1;
    int idx = g_docCount;
    Doc* d = &g_docs[idx];

    d->hwndEdit = CreateWindowExW(0, L"Scintilla", NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, g_hwndMain, NULL, g_hInst, NULL);
    if (!d->hwndEdit) { MessageBoxW(g_hwndMain, T(STR_MSG_SCINTILLA_FAIL), T(STR_ERROR), MB_ICONERROR); return -1; }

    SendMessage(d->hwndEdit, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    SendMessage(d->hwndEdit, SCI_SETEOLMODE, SC_EOL_LF, 0);
    SendMessage(d->hwndEdit, SCI_SETCARETWIDTH, 1, 0);
    SendMessage(d->hwndEdit, SCI_EMPTYUNDOBUFFER, 0, 0);
    SendMessage(d->hwndEdit, SCI_SETSAVEPOINT, 0, 0);
    ApplyScintillaStyle(d->hwndEdit);

    d->isNew = isNew;
    d->dirty = FALSE;
    d->eol = 1;
    d->draft[0] = L'\0';
    d->lang = LANG_NONE;
    d->previewOn = FALSE;
    if (isNew) {
        d->path[0] = L'\0';
        wcscpy_s(d->baseTitle, 256, title ? title : T(STR_UNTITLED));
        d->enc = ENC_UTF8;
    } else {
        wcscpy_s(d->path, MAX_PATH, path); d->path[MAX_PATH-1]=L'\0';
        d->enc = ENC_UTF8;
        d->baseTitle[0] = L'\0';
    }

    g_docCount++;
    Editor_UpdateTitle(idx);
    return idx;
}

static HWND s_hwndProg = NULL;
static BOOL s_progCancel = FALSE;

static INT_PTR CALLBACK ProgressProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SendMessageW(GetDlgItem(hdlg, IDC_PROG_BAR), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        return TRUE;
    }
    if (msg == WM_COMMAND &&
        (LOWORD(wp) == IDC_PROG_CANCEL || LOWORD(wp) == IDCANCEL)) {
        s_progCancel = TRUE;
        return TRUE;
    }
    (void)lp;
    return FALSE;
}

static void ProgressPump(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            s_progCancel = TRUE;
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!s_hwndProg || !IsDialogMessageW(s_hwndProg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

static void ProgressBegin(StrId verb, const wchar_t* path) {
    wchar_t label[MAX_PATH + 32];
    wchar_t name[MAX_PATH];
    wcscpy_s(name, MAX_PATH, path);
    PathStripPathW(name);
    wsprintf(label, L"%s %s", T(verb), name);
    s_progCancel = FALSE;
    s_hwndProg = CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_PROGRESS),
                                    g_hwndMain, ProgressProc, 0);
    if (!s_hwndProg) return;
    EnableWindow(g_hwndMain, FALSE);
    SetDlgItemTextW(s_hwndProg, IDC_PROG_TEXT, label);
    SetDlgItemTextW(s_hwndProg, IDC_PROG_CANCEL, T(STR_CANCEL));
    ShowWindow(s_hwndProg, SW_SHOW);
    ProgressPump();
}

static BOOL ProgressCb(UINT64 done, UINT64 total, void* ctx) {
    (void)ctx;
    if (s_hwndProg && total)
        SendDlgItemMessageW(s_hwndProg, IDC_PROG_BAR, PBM_SETPOS,
                            (int)(done * 100 / total), 0);
    ProgressPump();
    return !s_progCancel;
}

static void ProgressEnd(void) {
    if (s_hwndProg) { DestroyWindow(s_hwndProg); s_hwndProg = NULL; }
    EnableWindow(g_hwndMain, TRUE);
    if (g_hwndMain) SetFocus(g_hwndMain);
}

#define PROGRESS_MIN (4 * 1024 * 1024)

static BOOL ProgressWorthy(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!path || !path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return FALSE;
    ULARGE_INTEGER sz; sz.HighPart = fa.nFileSizeHigh; sz.LowPart = fa.nFileSizeLow;
    return sz.QuadPart >= PROGRESS_MIN;
}

static BOOL SaveProgressWorthy(const Doc* d) {
    if (ProgressWorthy(d->path)) return TRUE;
    return SendMessage(d->hwndEdit, SCI_GETLENGTH, 0, 0) >= PROGRESS_MIN;
}

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
    Doc* d = &g_docs[index];
    HWND hed = d->hwndEdit;
    Encoding detected = ENC_UTF8;
    int eol = 0;

    g_suppressDirty = TRUE;
    SendMessage(hed, WM_SETREDRAW, FALSE, 0);
    SendMessage(hed, SCI_SETUNDOCOLLECTION, FALSE, 0);
    SendMessage(hed, SCI_CLEARALL, 0, 0);
    BOOL prog = ProgressWorthy(path);
    if (prog) ProgressBegin(STR_LOAD_ING, path);
    BOOL ok = StreamLoadToDoc(hed, path, enc, &detected, &eol, ProgressCb, NULL);
    if (prog) ProgressEnd();
    SendMessage(hed, SCI_SETUNDOCOLLECTION, TRUE, 0);
    SendMessage(hed, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hed, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    g_suppressDirty = FALSE;
    if (!ok) return FALSE;

    int sci_eol = (eol == 0) ? SC_EOL_CRLF : (eol == 1 ? SC_EOL_LF : SC_EOL_CR);
    SendMessage(hed, SCI_SETEOLMODE, sci_eol, 0);
    SendMessage(hed, SCI_EMPTYUNDOBUFFER, 0, 0);
    SendMessage(hed, SCI_SETSAVEPOINT, 0, 0);
    d->eol = eol;
    d->enc = detected;
    d->isNew = FALSE;
    wcscpy_s(d->path, MAX_PATH, path);
    d->dirty = FALSE;
    d->lang = LangFromPath(path);
    ApplyLexer(d->hwndEdit, d->lang);
    DiskWriteTime(d->path, &d->ftWrite);
    Editor_UpdateTitle(index);

    if (Session_AutoBackupExists(path)) {
        char* bak = NULL; DWORD bakLen = 0;
        BOOL have = Session_AutoBackupRead(path, &bak, &bakLen);
        wchar_t msg[600];
        wsprintf(msg, T(STR_MSG_AUTOBACKUP), d->title);
        int r = MessageBoxW(g_hwndMain, msg, T(STR_TITLE_SAVE), MB_YESNO | MB_ICONQUESTION);
        if (r == IDYES && have) {
            Editor_SetText(index, bak);
            d->dirty = TRUE;
            Editor_UpdateTitle(index);
            InvalidateRect(g_hwndTab, NULL, FALSE);
        } else {
            Session_AutoBackupDiscard(path);
        }
        free(bak);
    }
    return TRUE;
}

void Editor_Activate(int index) {
    if (index < 0 || index >= g_docCount) return;
    g_curDoc = index;
    for (int i = 0; i < g_docCount; i++) {
        BOOL hideFull = g_docs[i].previewOn && g_docs[i].lang == LANG_MD && !g_mdSplit;
        BOOL show = (i == index) && !hideFull;
        ShowWindow(g_docs[i].hwndEdit, show ? SW_SHOW : SW_HIDE);
    }
    Editor_ApplyWrap(index);
    Editor_Layout();
    MdView_OnActivate();
    InvalidateRect(g_hwndTab, NULL, FALSE);
    Editor_UpdateStatus();
    Session_Save();
}

void Editor_ApplyWrap(int index) {
    if (index < 0 || index >= g_docCount) return;
    HWND hed = g_docs[index].hwndEdit;
    SendMessage(hed, SCI_SETWRAPMODE, g_wordWrap ? SC_WRAP_WORD : SC_WRAP_NONE, 0);
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
    if (!g_showGutter) {
        SendMessage(hed, SCI_SETMARGINWIDTHN, 0, 0);
        return;
    }
    int lines = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
    int digits = 3;
    int t = lines; while (t >= 10) { digits++; t /= 10; }
    char nines[24];
    memset(nines, '9', (size_t)digits);
    nines[digits] = '\0';
    int w = (int)SendMessage(hed, SCI_TEXTWIDTH, STYLE_LINENUMBER, (LPARAM)nines) + UI_Scale(8);
    if (w < UI_Scale(36)) w = UI_Scale(36);
    g_gutterWidth = w;
    SendMessage(hed, SCI_SETMARGINWIDTHN, 0, w);
}

void Editor_Layout(void) {
    RECT rc; GetClientRect(g_hwndMain, &rc);
    int cx = rc.right, cy = rc.bottom;
    int tabH = UI_Scale(TAB_HEIGHT_96);
    int statusH = UI_Scale(STATUS_HEIGHT_96);

    SetWindowPos(g_hwndTab, NULL, 0, 0, cx, tabH, SWP_NOZORDER);
    SetWindowPos(g_hwndStatus, NULL, 0, cy - statusH, cx, statusH, SWP_NOZORDER);

    int top = tabH;
    int bottom = cy - statusH;
    int h = bottom - top;

    if (MdView_IsVisible() && g_mdSplit) {
        int sbw = ScrollBars_Thickness();
        int splitX = cx / 2;
        int minSplit = UI_Scale(120) + sbw;
        if (splitX < minSplit) splitX = minSplit;
        int editW = splitX - sbw;
        BOOL showH = !g_wordWrap;
        ScrollBars_Layout(0, top, splitX, h, sbw, showH);
        if (g_curDoc >= 0 && g_curDoc < g_docCount) {
            Editor_ComputeGutterWidth(g_curDoc);
            SetWindowPos(g_docs[g_curDoc].hwndEdit, NULL, 0, top,
                         editW, h - (showH ? sbw : 0), SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        ScrollBars_Update();
        MdView_OnLayout(splitX, top, cx - splitX, h);
        return;
    }

    if (MdView_IsVisible()) {
        ScrollBars_Layout(0, 0, 0, 0, 0, FALSE);
        MdView_OnLayout(0, top, cx, h);
        return;
    }

    int sbw = ScrollBars_Thickness();
    BOOL showH = !g_wordWrap;

    ScrollBars_Layout(0, top, cx, h, sbw, showH);

    if (g_curDoc >= 0 && g_curDoc < g_docCount) {
        Editor_ComputeGutterWidth(g_curDoc);
        SetWindowPos(g_docs[g_curDoc].hwndEdit, NULL, 0, top,
                     cx - sbw, h - (showH ? sbw : 0), SWP_NOZORDER);
    }
    ScrollBars_Update();
}

void Editor_Zoom(int delta) {
    g_fontSize += delta;
    if (g_fontSize < 6) g_fontSize = 6;
    if (g_fontSize > 48) g_fontSize = 48;
    for (int i = 0; i < g_docCount; i++) {
        if (g_docs[i].hwndEdit) {
            SendMessage(g_docs[i].hwndEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, g_fontSize);
            SendMessage(g_docs[i].hwndEdit, SCI_STYLECLEARALL, 0, 0);
        }
    }
    MdView_OnZoom();
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

void Editor_UpdateGutter(int index) {
    if (index < 0 || index >= g_docCount) return;
    Editor_ComputeGutterWidth(index);
}

void Editor_OnNotify(LPARAM lp) {
    SCNotification* scn = (SCNotification*)lp;
    HWND hed = (HWND)scn->nmhdr.hwndFrom;
    int idx = Editor_IndexFromHwnd(hed);
    if (idx < 0) return;

    if (scn->nmhdr.code == SCN_UPDATEUI) {
        Editor_UpdateStatus();
        if (g_mdSplit && MdView_IsVisible() && !g_mdSyncLock &&
            (scn->updated & (SC_UPDATE_V_SCROLL | SC_UPDATE_SELECTION))) {
            int first = (int)SendMessage(hed, SCI_GETFIRSTVISIBLELINE, 0, 0);
            int total = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
            int page = (int)SendMessage(hed, SCI_LINESONSCREEN, 0, 0);
            int span = total - page;
            if (span <= 0) span = 1;
            double frac = (double)first / span;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            g_mdSyncLock = TRUE;
            MdView_SyncScrollFromEdit(frac);
            g_mdSyncLock = FALSE;
        }
    } else if (scn->nmhdr.code == SCN_MARGINCLICK) {
        if (scn->margin == 1) {
            int line = (int)SendMessage(hed, SCI_LINEFROMPOSITION, scn->position, 0);
            SendMessage(hed, SCI_TOGGLEFOLD, line, 0);
        }
    } else if (scn->nmhdr.code == SCN_MODIFIED) {
        if (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
            if (!g_suppressDirty) {
                Editor_MarkDirty(idx, TRUE);
                Editor_UpdateStatus();
                Editor_UpdateGutter(idx);
                if (g_docs[idx].isNew) g_draftsDirty = TRUE;
            }
        }
    } else if (scn->nmhdr.code == SCN_SAVEPOINTREACHED) {
        MdView_RefreshIfActive(idx);
    }
}

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

BOOL Editor_SaveDoc(int index, BOOL askPath) {
    Doc* d = &g_docs[index];
    if (askPath || d->isNew || d->path[0] == L'\0') {
        return Editor_SaveDocAs(index);
    }
    if (d->enc == ENC_ANSI && !ConfirmAnsiOk(d->hwndEdit)) return FALSE;
    if (!ConfirmOverwriteOk(d)) return FALSE;
    BOOL prog = SaveProgressWorthy(d);
    if (prog) ProgressBegin(STR_SAVE_ING, d->path);
    BOOL ok = StreamSaveFromDoc(d->hwndEdit, d->path, d->enc, d->eol, ProgressCb, NULL);
    if (prog) ProgressEnd();
    if (ok) {
        d->dirty = FALSE;
        SendMessage(d->hwndEdit, SCI_SETSAVEPOINT, 0, 0);
        AddRecent(d->path);
        DiskWriteTime(d->path, &d->ftWrite);
        Session_AutoBackupDiscard(d->path);
        Editor_UpdateTitle(index);
        InvalidateRect(g_hwndTab, NULL, FALSE);
    } else {
        MessageBoxW(g_hwndMain, T(STR_MSG_SAVE_FAILED), T(STR_ERROR), MB_ICONERROR);
    }
    return ok;
}

BOOL Editor_SaveDocAs(int index) {
    Doc* d = &g_docs[index];
    wchar_t fname[MAX_PATH] = {0};
    if (!d->isNew) { wcscpy_s(fname, MAX_PATH, d->path); PathStripPathW(fname); }
    else wcscpy_s(fname, MAX_PATH, T(STR_UNTITLED_TXT));

    wchar_t filter[256];
    I18n_JoinFilter(filter, 256, STR_FILTER_TXT, STR_FILTER_TXTPAT,
                             STR_FILTER_ALL, STR_FILTER_ALLPAT);
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
    d->baseTitle[0] = L'\0';
    if (d->enc == ENC_ANSI && !ConfirmAnsiOk(d->hwndEdit)) return FALSE;
    BOOL prog = SaveProgressWorthy(d);
    if (prog) ProgressBegin(STR_SAVE_ING, d->path);
    BOOL ok = StreamSaveFromDoc(d->hwndEdit, d->path, d->enc, d->eol, ProgressCb, NULL);
    if (prog) ProgressEnd();
    if (ok) {
        d->dirty = FALSE;
        SendMessage(d->hwndEdit, SCI_SETSAVEPOINT, 0, 0);
        AddRecent(d->path);
        DiskWriteTime(d->path, &d->ftWrite);
        Session_AutoBackupDiscard(d->path);
        Editor_UpdateTitle(index);
        InvalidateRect(g_hwndTab, NULL, FALSE);
        Session_DiscardDraft(index);
        Session_SaveDrafts();
    }
    return ok;
}

void Editor_CloseDoc(int index) {
    if (index < 0 || index >= g_docCount) return;
    if (!g_docs[index].isNew && g_docs[index].dirty) {
        wchar_t msg[300];
        wsprintf(msg, T(STR_MSG_FILE_MODIFIED), g_docs[index].title);
        int r = MessageBoxW(g_hwndMain, msg, T(STR_APP_TITLE), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) { if (!Editor_SaveDoc(index, FALSE)) return; }
    } else if (g_docs[index].isNew) {
        Session_DiscardDraft(index);
    }
    if (!g_docs[index].isNew && g_docs[index].path[0])
        Session_AutoBackupDiscard(g_docs[index].path);
    DestroyWindow(g_docs[index].hwndEdit);
    for (int i = index; i < g_docCount - 1; i++) g_docs[i] = g_docs[i + 1];
    g_docCount--;
    if (g_docCount == 0) {
        int ni = Editor_NewDoc(NULL, T(STR_UNTITLED), TRUE);
        Editor_Activate(ni);
    } else {
        if (index < g_curDoc) g_curDoc--;
        if (g_curDoc >= g_docCount) g_curDoc = g_docCount - 1;
        if (g_curDoc >= 0) Editor_Activate(g_curDoc);
    }
    Session_SaveDrafts();
}

int Editor_FindDocByPath(const wchar_t* path) {
    for (int i = 0; i < g_docCount; i++) {
        if (!g_docs[i].isNew && _wcsicmp(g_docs[i].path, path) == 0) return i;
    }
    return -1;
}

void OpenFileByPath(const wchar_t* path) {
    int ex = Editor_FindDocByPath(path);
    if (ex >= 0) { Editor_Activate(ex); return; }
    int ni = Editor_NewDoc(path, NULL, FALSE);
    if (ni < 0) return;
    if (!Editor_LoadFile(ni, path, ENC_AUTO)) { Editor_CloseDoc(ni); return; }
    AddRecent(path);
    Editor_Activate(ni);
}

HWND Editor_ActiveEdit(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return NULL;
    return g_docs[g_curDoc].hwndEdit;
}

void Editor_UpdateStatus(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) {
        StatusBar_SetText(0, L"");
        ScrollBars_Update();
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
        wsprintf(s1, T(STR_STATUS_SEL_CHARS), (int)(selEnd - selStart));
    else
        wsprintf(s1, T(STR_STATUS_CHARS), (int)len);
    wsprintf(s2, T(STR_STATUS_LN), line + 1, totalLines);
    wsprintf(s3, T(STR_STATUS_COL), col);
    wsprintf(s4, L"%s", EncodingName(d->enc));
    const wchar_t* eolname = d->eol == 0 ? L"CRLF" : (d->eol == 1 ? L"LF" : L"CR");
    wsprintf(s5, L"%s  |  %d%%", eolname, g_fontSize * 10);
    StatusBar_SetText(0, s1);
    StatusBar_SetText(1, s2);
    StatusBar_SetText(2, s3);
    StatusBar_SetText(3, s4);
    StatusBar_SetText(4, s5);
    ScrollBars_Update();
}

void Editor_OnLanguageChanged(void) {
    for (int i = 0; i < g_docCount; i++) {
        Doc* d = &g_docs[i];
        if (!d->isNew || d->path[0] != L'\0') continue;
        int n;
        if (wcscmp(d->baseTitle, L"未命名") == 0 || wcscmp(d->baseTitle, L"Untitled") == 0) {
            wcscpy_s(d->baseTitle, 256, T(STR_UNTITLED));
        } else if (swscanf(d->baseTitle, L"未命名 %d", &n) == 1 ||
                   swscanf(d->baseTitle, L"Untitled %d", &n) == 1) {
            wchar_t t[64];
            wsprintf(t, T(STR_UNTITLED_N), n);
            wcscpy_s(d->baseTitle, 256, t);
        } else {
            continue;
        }
        Editor_UpdateTitle(i);
    }
    InvalidateRect(g_hwndTab, NULL, FALSE);
    Editor_UpdateStatus();
}

#define BM_MARKER 24
#define BM_MASK   (1 << BM_MARKER)

void Editor_ToggleBookmark(int index) {
    if (index < 0 || index >= g_docCount) return;
    HWND hed = g_docs[index].hwndEdit;
    int line = (int)SendMessage(hed, SCI_LINEFROMPOSITION,
                                (WPARAM)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0), 0);
    if (SendMessage(hed, SCI_MARKERGET, line, 0) & BM_MASK)
        SendMessage(hed, SCI_MARKERDELETE, line, BM_MARKER);
    else
        SendMessage(hed, SCI_MARKERADD, line, BM_MARKER);
}

void Editor_GotoBookmark(int index, BOOL next) {
    if (index < 0 || index >= g_docCount) return;
    HWND hed = g_docs[index].hwndEdit;
    int total = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
    int line = (int)SendMessage(hed, SCI_LINEFROMPOSITION,
                                (WPARAM)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0), 0);
    int target;
    if (next) {
        target = (int)SendMessage(hed, SCI_MARKERNEXT, line + 1, BM_MASK);
        if (target < 0) target = (int)SendMessage(hed, SCI_MARKERNEXT, 0, BM_MASK);
    } else {
        target = (int)SendMessage(hed, SCI_MARKERPREVIOUS, line - 1, BM_MASK);
        if (target < 0) target = (int)SendMessage(hed, SCI_MARKERPREVIOUS, total, BM_MASK);
    }
    if (target >= 0) SendMessage(hed, SCI_GOTOLINE, target, 0);
}

BOOL DiskWriteTimePublic(const wchar_t* path, FILETIME* ft) {
    return DiskWriteTime(path, ft);
}

void Editor_SyncScrollFromPreview(double frac) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    HWND hed = g_docs[g_curDoc].hwndEdit;
    if (!hed || !IsWindowVisible(hed)) return;
    int total = (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0);
    int page = (int)SendMessage(hed, SCI_LINESONSCREEN, 0, 0);
    int span = total - page;
    if (span <= 0) return;
    int line = (int)(frac * span + 0.5);
    if (line < 0) line = 0;
    if (line > span) line = span;
    SendMessage(hed, SCI_SETFIRSTVISIBLELINE, line, 0);
}
