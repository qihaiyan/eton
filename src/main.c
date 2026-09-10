#include "common.h"
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")

/* ---------------- globals ---------------- */
HINSTANCE g_hInst;
HWND g_hwndMain, g_hwndTab, g_hwndStatus;
UINT g_dpi = 96;

int UI_Scale(int px) {
    return MulDiv(px, (int)g_dpi, 96);
}
Doc g_docs[MAX_DOCS];
int g_docCount = 0;
int g_curDoc = -1;
HFONT g_hFont = NULL;
int g_fontSize = 11;
BOOL g_wordWrap = FALSE;
BOOL g_showGutter = TRUE;
BOOL g_dark = FALSE;
int g_gutterWidth = 40;
wchar_t g_recent[MAX_RECENT][MAX_PATH];
int g_recentCount = 0;
COLORREF g_clrBg, g_clrFg, g_clrSelBg, g_clrGutterBg, g_clrGutterFg, g_clrStatusBg, g_clrStatusFg;
BOOL g_suppressDirty = FALSE;
BOOL g_draftsDirty = FALSE;   /* 有未命名文档内容变化，待写草稿 */

void ShowError(const wchar_t* msg) {
    MessageBoxW(NULL, msg, T(STR_APP_TITLE), MB_ICONERROR);
}

/* ---------------- 深色标题栏 ---------------- */
void ApplyTitleBarTheme(HWND hwnd) {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    /* 动态加载 dwmapi（进程默认未加载它，须用 LoadLibrary），失败静默 */
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT (WINAPI *PFN)(HWND, DWORD, LPCVOID, DWORD);
    PFN p = (PFN)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!p) return;
    BOOL v = g_dark ? TRUE : FALSE;
    p(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ---------------- recent files ---------------- */
void AddRecent(const wchar_t* path) {
    for (int i = 0; i < g_recentCount; i++) {
        if (_wcsicmp(g_recent[i], path) == 0) {
            for (int j = i; j < g_recentCount - 1; j++) wcscpy_s(g_recent[j], MAX_PATH, g_recent[j + 1]);
            g_recentCount--; break;
        }
    }
    if (g_recentCount < MAX_RECENT) {
        for (int i = g_recentCount; i > 0; i--) wcscpy_s(g_recent[i], MAX_PATH, g_recent[i - 1]);
        wcscpy_s(g_recent[0], MAX_PATH, path); g_recentCount++;
    } else {
        for (int i = MAX_RECENT - 1; i > 0; i--) wcscpy_s(g_recent[i], MAX_PATH, g_recent[i - 1]);
        wcscpy_s(g_recent[0], MAX_PATH, path);
    }
    RefreshRecentMenu();
}

void RefreshRecentMenu(void) {
    /* 最近文件子菜单由 i18n.c 建菜单时记录，不再按菜单文字反查位置 */
    if (!g_hMenuRecent) return;
    HMENU sub = g_hMenuRecent;
    int cnt;
    for (int guard = 0; guard < 256 && (cnt = GetMenuItemCount(sub)) > 0; guard++)
        RemoveMenu(sub, cnt - 1, MF_BYPOSITION);
    if (g_recentCount == 0) {
        I18n_OwnerAppend(sub, MF_STRING | MF_GRAYED, IDM_RECENT_FIRST, T(STR_RECENT_EMPTY));
    } else {
        for (int i = 0; i < g_recentCount; i++)
            I18n_OwnerAppend(sub, MF_STRING, IDM_RECENT_FIRST + i, g_recent[i]);
    }
}

/* ---------------- menu rebuild ---------------- */
/* 语言切换 / 主题切换时重建整份菜单栏（底色刷、自绘项、文字全部刷新） */
static void RebuildMainMenu(HWND hwnd) {
    HMENU old = GetMenu(hwnd);
    SetMenu(hwnd, I18n_BuildMainMenu());
    if (old) DestroyMenu(old);
    RefreshRecentMenu();
}

/* ---------------- menu checks ---------------- */
static void Check(HMENU m, UINT id, BOOL on) {
    CheckMenuItem(m, id, on ? MF_CHECKED : MF_UNCHECKED);
}
static void UpdateMenuChecks(void) {
    HMENU m = GetMenu(g_hwndMain);
    if (!m) return;
    Check(m, IDM_WRAP, g_wordWrap);
    Check(m, IDM_GUTTER, g_showGutter);
    Check(m, IDM_THEME, g_dark);
    /* Markdown 预览：仅当前文档为 Markdown 时可用 */
    BOOL isMd = (g_curDoc >= 0 && g_docs[g_curDoc].lang == LANG_MD);
    Check(m, IDM_VIEW_MD, isMd && g_docs[g_curDoc].previewOn);
    EnableMenuItem(m, IDM_VIEW_MD, isMd ? MF_BYCOMMAND | MF_ENABLED
                                        : MF_BYCOMMAND | MF_GRAYED);
    Encoding e = (g_curDoc >= 0) ? g_docs[g_curDoc].enc : ENC_UTF8;
    LangID lg = (g_curDoc >= 0) ? g_docs[g_curDoc].lang : LANG_NONE;
    int eol = (g_curDoc >= 0) ? g_docs[g_curDoc].eol : 0;
    for (int i = 0; i <= 4; i++) Check(m, IDM_ENC_ANSI + i, (Encoding)i == e);
    for (int i = 0; i <= 4; i++) Check(m, IDM_REOPEN_ANSI + i, FALSE);
    for (int i = 0; i <= 2; i++) Check(m, IDM_EOL_CRLF + i, i == eol);
    for (int i = 0; i <= (IDM_LANG_LAST - IDM_LANG_FIRST); i++)
        Check(m, IDM_LANG_NONE + i, (LangID)i == lg);
    /* 界面语言 */
    Check(m, IDM_UI_LANG_ZH, I18n_Lang() == UI_LANG_ZH);
    Check(m, IDM_UI_LANG_EN, I18n_Lang() == UI_LANG_EN);

    /* 按编辑器/剪贴板实际状态置灰菜单（WM_INITMENU 时调用） */
    HWND edit = Editor_ActiveEdit();
    BOOL hasDoc = (edit != NULL);
    BOOL canUndo = hasDoc && SendMessage(edit, SCI_CANUNDO, 0, 0) != 0;
    BOOL canRedo = hasDoc && SendMessage(edit, SCI_CANREDO, 0, 0) != 0;
    BOOL canPaste = hasDoc && SendMessage(edit, SCI_CANPASTE, 0, 0) != 0;
    BOOL hasSel = hasDoc && (SendMessage(edit, SCI_GETSELECTIONSTART, 0, 0) !=
                             SendMessage(edit, SCI_GETSELECTIONEND, 0, 0));
    UINT on = MF_BYCOMMAND | MF_ENABLED, off = MF_BYCOMMAND | MF_GRAYED;
    EnableMenuItem(m, IDM_UNDO, canUndo ? on : off);
    EnableMenuItem(m, IDM_REDO, canRedo ? on : off);
    EnableMenuItem(m, IDM_CUT, hasSel ? on : off);
    EnableMenuItem(m, IDM_COPY, hasSel ? on : off);
    EnableMenuItem(m, IDM_DELETE, hasSel ? on : off);
    EnableMenuItem(m, IDM_PASTE, canPaste ? on : off);
    EnableMenuItem(m, IDM_SELECTALL, hasDoc ? on : off);
    EnableMenuItem(m, IDM_SAVE, hasDoc ? on : off);
    EnableMenuItem(m, IDM_SAVEAS, hasDoc ? on : off);
    EnableMenuItem(m, IDM_CLOSE, hasDoc ? on : off);
}

/* ---------------- file open helpers ---------------- */
static void OpenFiles(HWND hwnd) {
    wchar_t buf[32768]; memset(buf, 0, sizeof(buf));
    wchar_t filter[512];
    I18n_JoinFilter(filter, 512, STR_FILTER_ALL, STR_FILTER_ALLPAT,
                             STR_FILTER_TEXTS, STR_FILTER_TEXTSPAT);
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    if (buf[ofn.nFileOffset - 1] != L'\0') {
        OpenFileByPath(buf);
    } else {
        wchar_t dir[MAX_PATH]; wcscpy_s(dir, MAX_PATH, buf);
        wchar_t* p = buf + wcslen(buf) + 1;
        while (*p) {
            wchar_t full[MAX_PATH];
            wcscpy_s(full, MAX_PATH, dir); wcscat_s(full, MAX_PATH, L"\\"); wcscat_s(full, MAX_PATH, p);
            OpenFileByPath(full);
            p += wcslen(p) + 1;
        }
    }
}

static void OpenRecent(int ri) {
    if (ri < 0 || ri >= g_recentCount) return;
    wchar_t path[MAX_PATH]; wcscpy_s(path, MAX_PATH, g_recent[ri]);
    if (!PathFileExistsW(path)) {
        for (int j = ri; j < g_recentCount - 1; j++) wcscpy_s(g_recent[j], MAX_PATH, g_recent[j + 1]);
        g_recentCount--;
        return;
    }
    OpenFileByPath(path);
}


/* ---------------- misc ---------------- */
static void InsertDateTime(void) {
    HWND edit = Editor_ActiveEdit();
    if (!edit) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[64];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SendMessage(edit, SCI_REPLACESEL, 0, (LPARAM)buf);
}

/* Convert wchar_t search text to UTF-8 for Scintilla */
static void WToUtf8(const wchar_t* w, char* out, int outlen) {
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outlen, NULL, NULL);
}

static void FindNextAccel(HWND hwnd, BOOL down) {
    HWND edit = Editor_ActiveEdit();
    if (!edit) return;
    if (wcslen(g_findText) == 0) { Dlg_Find(hwnd, FALSE); return; }
    char utf8[1024];
    WToUtf8(g_findText, utf8, sizeof(utf8));
    Sci_Position len = (Sci_Position)strlen(utf8);
    (void)len;
    Sci_Position curStart = (Sci_Position)SendMessage(edit, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position selStart = (Sci_Position)SendMessage(edit, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = (Sci_Position)SendMessage(edit, SCI_GETSELECTIONEND, 0, 0);
    Sci_Position start = down ? selEnd : selStart;
    (void)curStart;
    /* Set search target */
    if (down) {
        SendMessage(edit, SCI_SETTARGETSTART, start, 0);
        SendMessage(edit, SCI_SETTARGETEND, (WPARAM)SendMessage(edit, SCI_GETLENGTH, 0, 0), 0);
    } else {
        SendMessage(edit, SCI_SETTARGETSTART, start, 0);
        SendMessage(edit, SCI_SETTARGETEND, 0, 0);
    }
    int flags = 0;
    if (g_findCase) flags |= SCFIND_MATCHCASE;
    if (g_findWord) flags |= SCFIND_WHOLEWORD;
    if (g_findRegex) flags |= SCFIND_REGEXP;
    SendMessage(edit, SCI_SETSEARCHFLAGS, flags, 0);
    Sci_Position pos = (Sci_Position)SendMessage(edit, SCI_SEARCHINTARGET, len, (LPARAM)utf8);
    Sci_Position hitEnd;
    if (pos >= 0) {
        hitEnd = (Sci_Position)SendMessage(edit, SCI_GETTARGETEND, 0, 0);
        SendMessage(edit, SCI_SETSEL, pos, hitEnd);
        SendMessage(edit, SCI_SCROLLCARET, 0, 0);
        Find_MarkAll(edit, g_findText, g_findCase, g_findWord, g_findRegex);
    } else {
        /* wrap */
        if (down) {
            SendMessage(edit, SCI_SETTARGETSTART, 0, 0);
            SendMessage(edit, SCI_SETTARGETEND, start, 0);
        } else {
            SendMessage(edit, SCI_SETTARGETSTART, (WPARAM)SendMessage(edit, SCI_GETLENGTH, 0, 0), 0);
            SendMessage(edit, SCI_SETTARGETEND, start, 0);
        }
        pos = (Sci_Position)SendMessage(edit, SCI_SEARCHINTARGET, len, (LPARAM)utf8);
        if (pos >= 0) {
            hitEnd = (Sci_Position)SendMessage(edit, SCI_GETTARGETEND, 0, 0);
            SendMessage(edit, SCI_SETSEL, pos, hitEnd);
            SendMessage(edit, SCI_SCROLLCARET, 0, 0);
            Find_MarkAll(edit, g_findText, g_findCase, g_findWord, g_findRegex);
        } else {
            MessageBoxW(hwnd, T(STR_MSG_NOT_FOUND), T(STR_TITLE_FIND), MB_OK | MB_ICONINFORMATION);
        }
    }
}

static void ResizeStatus(void) {
    RECT rc; GetClientRect(g_hwndMain, &rc);
    int cx = rc.right;
    int p[5];
    p[0] = cx * 45 / 100; p[1] = cx * 65 / 100; p[2] = cx * 78 / 100; p[3] = cx * 90 / 100; p[4] = -1;
    StatusBar_SetParts(p, 5);
}

static BOOL ConfirmExit(void) {
    for (int i = 0; i < g_docCount; i++) {
        if (g_docs[i].dirty && !g_docs[i].isNew) {   /* 草稿自动保存，无需确认 */
            wchar_t msg[300];
            wsprintf(msg, T(STR_MSG_FILE_NOT_SAVED), g_docs[i].title);
            int r = MessageBoxW(g_hwndMain, msg, T(STR_APP_TITLE), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return FALSE;
            if (r == IDYES) { if (!Editor_SaveDoc(i, FALSE)) return FALSE; }
        }
    }
    return TRUE;
}

/* ---------------- command ---------------- */
static LRESULT OnCommand(HWND hwnd, WPARAM wp, LPARAM lp) {
    int id = LOWORD(wp);
    HWND edit = Editor_ActiveEdit();

    switch (id) {
        case IDM_NEW: { int ni = Editor_NewDoc(NULL, T(STR_UNTITLED), TRUE); if (ni >= 0) Editor_Activate(ni); } break;
        case IDM_OPEN: OpenFiles(hwnd); break;
        case IDM_OPENENC: Dlg_OpenEnc(hwnd); break;
        case IDM_SAVE: if (g_curDoc >= 0) Editor_SaveDoc(g_curDoc, FALSE); break;
        case IDM_SAVEAS: if (g_curDoc >= 0) Editor_SaveDocAs(g_curDoc); break;
        case IDM_CLOSE: if (g_curDoc >= 0) Editor_CloseDoc(g_curDoc); break;
        case IDM_EXIT: SendMessage(hwnd, WM_CLOSE, 0, 0); break;
        case IDM_UNDO: if (edit) SendMessage(edit, SCI_UNDO, 0, 0); break;
        case IDM_REDO: if (edit) SendMessage(edit, SCI_REDO, 0, 0); break;
        case IDM_CUT: if (edit) SendMessage(edit, SCI_CUT, 0, 0); break;
        case IDM_COPY: if (edit) SendMessage(edit, SCI_COPY, 0, 0); break;
        case IDM_PASTE: if (edit) SendMessage(edit, SCI_PASTE, 0, 0); break;
        case IDM_DELETE: if (edit) SendMessage(edit, SCI_CLEAR, 0, 0); break;
        case IDM_SELECTALL: if (edit) SendMessage(edit, SCI_SELECTALL, 0, 0); break;
        case IDM_FIND: Dlg_Find(hwnd, FALSE); break;
        case IDM_REPLACE: Dlg_Find(hwnd, TRUE); break;
        case IDM_GOTO: Dlg_Goto(hwnd); break;
        case IDM_TIME: InsertDateTime(); break;
        case IDM_JSON_FMT: Json_FormatActiveDoc(FALSE); break;
        case IDM_JSON_MIN: Json_FormatActiveDoc(TRUE); break;
        case IDM_WRAP:
            g_wordWrap = !g_wordWrap;
            if (g_curDoc >= 0) Editor_ApplyWrap(g_curDoc);
            UpdateMenuChecks(); break;
        case IDM_GUTTER:
            g_showGutter = !g_showGutter;
            if (g_curDoc >= 0) {
                /* Scintilla line-number margin: show/hide by width */
                SendMessage(g_docs[g_curDoc].hwndEdit, SCI_SETMARGINWIDTHN, 0,
                            g_showGutter ? g_gutterWidth : 0);
                Editor_Layout();
            }
            UpdateMenuChecks(); break;
        case IDM_ZOOMIN: Editor_Zoom(1); break;
        case IDM_ZOOMOUT: Editor_Zoom(-1); break;
        case IDM_ZOOMRST: g_fontSize = 11; Editor_SetFont(); Editor_UpdateStatus(); break;
        case IDM_THEME: {
            g_dark = !g_dark; Editor_ApplyThemeColors();
            I18n_ApplySystemThemeMode();   /* 系统箭头/消息框明暗跟随 */
            for (int i = 0; i < g_docCount; i++) Editor_ApplyTheme(i);
            MdView_OnThemeChange();        /* 预览配色现算现用，重画即可 */
            ApplyTitleBarTheme(hwnd);
            InvalidateRect(g_hwndTab, NULL, FALSE);
            InvalidateRect(g_hwndStatus, NULL, FALSE);
            ScrollBars_Repaint();
            TabBar_ApplyTheme();
            /* 重建菜单栏：底色刷与自绘项按新主题刷新 */
            RebuildMainMenu(hwnd);
            UpdateMenuChecks();
            break;
        }
        case IDM_ENC_ANSI: case IDM_ENC_UTF8: case IDM_ENC_UTF8BOM: case IDM_ENC_UTF16LE: case IDM_ENC_UTF16BE:
            if (g_curDoc >= 0) { g_docs[g_curDoc].enc = (Encoding)(id - IDM_ENC_ANSI); UpdateMenuChecks(); } break;
        case IDM_REOPEN_ANSI: case IDM_REOPEN_UTF8: case IDM_REOPEN_UTF8BOM: case IDM_REOPEN_UTF16LE: case IDM_REOPEN_UTF16BE:
            if (g_curDoc >= 0 && !g_docs[g_curDoc].isNew) {
                Encoding e = (Encoding)(id - IDM_REOPEN_ANSI);
                Editor_LoadFile(g_curDoc, g_docs[g_curDoc].path, e);
                Editor_Activate(g_curDoc);
            } break;
        case IDM_EOL_CRLF: case IDM_EOL_LF: case IDM_EOL_CR:
            if (g_curDoc >= 0) { g_docs[g_curDoc].eol = id - IDM_EOL_CRLF; Editor_UpdateStatus(); UpdateMenuChecks(); } break;
        case IDM_EOL_CONVERT: if (g_curDoc >= 0) Editor_ConvertEol(g_curDoc, g_docs[g_curDoc].eol); break;
        case IDM_LANG_NONE: case IDM_LANG_C: case IDM_LANG_CPP: case IDM_LANG_CS: case IDM_LANG_JAVA:
        case IDM_LANG_JS: case IDM_LANG_PY: case IDM_LANG_XML: case IDM_LANG_JSON: case IDM_LANG_SQL:
        case IDM_LANG_MD:
            if (g_curDoc >= 0) { Editor_SetLang(g_curDoc, (LangID)(id - IDM_LANG_NONE)); UpdateMenuChecks(); } break;
        case IDM_VIEW_MD:
            MdView_Toggle();
            break;
        case IDM_FINDNEXT: FindNextAccel(hwnd, TRUE); break;
        case IDM_FINDPREV: FindNextAccel(hwnd, FALSE); break;
        case IDM_ABOUT: Dlg_About(hwnd); break;
        case IDM_NEXT_TAB:
            if (g_docCount > 1) Editor_Activate((g_curDoc + 1) % g_docCount);
            break;
        case IDM_PREV_TAB:
            if (g_docCount > 1) Editor_Activate((g_curDoc + g_docCount - 1) % g_docCount);
            break;
        case IDM_CLOSE_OTHER:
            /* 关闭激活标签之外的所有标签：从尾部向前关，激活索引不受影响 */
            for (int i = g_docCount - 1; i >= 0; i--)
                if (i != g_curDoc) Editor_CloseDoc(i);
            break;
        case IDM_CLOSE_ALL:
            /* 从尾部关到只剩 1 个（关最后一个时编辑器会自动补一个空白标签） */
            while (g_docCount > 1) Editor_CloseDoc(g_docCount - 1);
            break;
        case IDM_BM_TOGGLE: if (g_curDoc >= 0) Editor_ToggleBookmark(g_curDoc); break;
        case IDM_BM_NEXT:   if (g_curDoc >= 0) Editor_GotoBookmark(g_curDoc, TRUE); break;
        case IDM_BM_PREV:   if (g_curDoc >= 0) Editor_GotoBookmark(g_curDoc, FALSE); break;
        case IDM_EXPLORER:
            if (g_curDoc >= 0 && !g_docs[g_curDoc].isNew && g_docs[g_curDoc].path[0]) {
                wchar_t params[MAX_PATH + 16];
                wsprintf(params, L"/select,\"%s\"", g_docs[g_curDoc].path);
                ShellExecuteW(hwnd, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
            }
            break;
        case IDM_UI_LANG_ZH: case IDM_UI_LANG_EN:
            if (I18n_Lang() != id - IDM_UI_LANG_ZH) {
                I18n_SetLang(id - IDM_UI_LANG_ZH);
                if (g_hFindDlg) { DestroyWindow(g_hFindDlg); g_hFindDlg = NULL; }  /* 关闭旧语言查找框 */
                RebuildMainMenu(hwnd);
                UpdateMenuChecks();
                Editor_OnLanguageChanged();   /* 未命名标签改用新语言标题，状态栏同步 */
                InvalidateRect(g_hwndTab, NULL, FALSE);
            }
            break;
        default:
            if (id >= IDM_RECENT_FIRST && id <= IDM_RECENT_LAST) OpenRecent(id - IDM_RECENT_FIRST);
            break;
    }
    return 0;
}

/* ---------------- 右键菜单 ---------------- */
/* 编辑区右键：剪贴板操作 + 全选 + 打开所在文件夹（按状态置灰） */
static void ShowEditContextMenu(HWND hwnd, POINT pt) {
    HWND edit = Editor_ActiveEdit();
    if (!edit) return;
    BOOL hasSel = SendMessage(edit, SCI_GETSELECTIONSTART, 0, 0) !=
                  SendMessage(edit, SCI_GETSELECTIONEND, 0, 0);
    BOOL canPaste = SendMessage(edit, SCI_CANPASTE, 0, 0) != 0;
    HMENU m = CreatePopupMenu();
    UINT on = MF_STRING, off = MF_STRING | MF_GRAYED;
    I18n_OwnerAppend(m, hasSel ? on : off, IDM_CUT, T(STR_ITEM_CUT));
    I18n_OwnerAppend(m, hasSel ? on : off, IDM_COPY, T(STR_ITEM_COPY));
    I18n_OwnerAppend(m, canPaste ? on : off, IDM_PASTE, T(STR_ITEM_PASTE));
    I18n_OwnerAppend(m, hasSel ? on : off, IDM_DELETE, T(STR_ITEM_DELETE));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    I18n_OwnerAppend(m, on, IDM_SELECTALL, T(STR_ITEM_SELECTALL));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    if (g_curDoc >= 0 && !g_docs[g_curDoc].isNew && g_docs[g_curDoc].path[0])
        I18n_OwnerAppend(m, on, IDM_EXPLORER, T(STR_ITEM_EXPLORER));
    I18n_ApplyMenuTheme(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (cmd) PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

/* 标签栏右键：关闭当前悬停标签 / 关闭其他 / 关闭所有 */
static void ShowTabContextMenu(HWND hwnd, POINT pt, int tabIndex) {
    HMENU m = CreatePopupMenu();
    UINT on = MF_STRING, off = MF_STRING | MF_GRAYED;
    if (tabIndex >= 0) I18n_OwnerAppend(m, on, IDM_CLOSE, T(STR_ITEM_CLOSE));
    I18n_OwnerAppend(m, (g_docCount > 1) ? on : off, IDM_CLOSE_OTHER, T(STR_ITEM_CLOSE_OTHER));
    I18n_OwnerAppend(m, (g_docCount > 1) ? on : off, IDM_CLOSE_ALL, T(STR_ITEM_CLOSE_ALL));
    I18n_ApplyMenuTheme(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (!cmd) return;
    if (cmd == IDM_CLOSE && tabIndex >= 0) {
        PostMessage(hwnd, WM_APP_TABCLOSE, (WPARAM)tabIndex, 0);
    } else if (cmd == IDM_CLOSE_OTHER) {
        /* 先切到悬停标签（保留它），再关闭其余 */
        if (tabIndex >= 0) PostMessage(hwnd, WM_APP_TABSEL, (WPARAM)tabIndex, 0);
        PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDM_CLOSE_OTHER, 0), 0);
    } else if (cmd == IDM_CLOSE_ALL) {
        PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDM_CLOSE_ALL, 0), 0);
    }
}

/* ---------------- 窗口过程 ---------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES;   /* WIN95: 进度条(加载/保存进度框) */
            InitCommonControlsEx(&icex);
            ScrollBars_Create(hwnd);   /* 自绘滚动条（须先于 Editor_Init 的首次布局） */
            Editor_Init();
            SetMenu(hwnd, I18n_BuildMainMenu());   /* 菜单按界面语言在代码中构建 */
            g_hwndTab = CreateWindowExW(0, L"NPPTabBar", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            StatusBar_Register();
            g_hwndStatus = CreateWindowExW(0, L"ETONStatus", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            ResizeStatus();
            DragAcceptFiles(hwnd, TRUE);   /* 接收从资源管理器拖入的文件 */

            /* 打开命令行指定的全部文件（eton.exe 文件1 文件2 ...） */
            int argc = 0;
            wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv) {
                for (int i = 1; i < argc; i++)
                    if (argv[i][0]) OpenFileByPath(argv[i]);
                LocalFree(argv);
            }
            BOOL haveArg = (g_docCount > 0);

            /* 无命令行参数时恢复上次会话；两者都没打开文件才新建空文档 */
            if (!haveArg && Session_Restore() == 0) {
                int ni = Editor_NewDoc(NULL, T(STR_UNTITLED), TRUE);
                Editor_Activate(ni);
            }
            SetTimer(hwnd, 1, 10000, NULL);   /* 定时落盘未命名文档草稿 */
            ApplyTitleBarTheme(hwnd);   /* 标题栏深浅跟随主题（设置在启动时已加载） */
            RefreshRecentMenu();
            return 0;
        }
        case WM_SIZE:
            ResizeStatus();
            Editor_Layout();
            return 0;
        case WM_DPICHANGED: {
            /* 跨显示器移动时按新 DPI 重设窗口尺寸（按系统建议矩形）并重排界面 */
            g_dpi = HIWORD(wp);
            const RECT* sug = (const RECT*)lp;
            SetWindowPos(hwnd, NULL, sug->left, sug->top,
                         sug->right - sug->left, sug->bottom - sug->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Editor_OnDpiChanged();
            return 0;
        }
        case WM_COMMAND:
            return OnCommand(hwnd, wp, lp);
        case WM_DROPFILES: {
            /* 打开拖入的文件（多个依次打开；目录忽略），已打开的文件切换到对应标签 */
            HDROP drop = (HDROP)wp;
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++) {
                wchar_t path[MAX_PATH];
                if (DragQueryFileW(drop, i, path, MAX_PATH) == 0) continue;
                DWORD attrs = GetFileAttributesW(path);
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) continue;
                OpenFileByPath(path);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_NOTIFY:
            /* Scintilla notifications come through here as SCN_* */
            Editor_OnNotify(lp);
            return 0;
        case WM_APP_TABSEL:
            Editor_Activate((int)wp); return 0;
        case WM_APP_TABCLOSE:
            Editor_CloseDoc((int)wp); return 0;
        case WM_INITMENU:
            UpdateMenuChecks();
            return 0;
        case WM_MEASUREITEM:
            if (I18n_OnMeasureItem(hwnd, (MEASUREITEMSTRUCT*)lp)) return TRUE;
            break;
        case WM_DRAWITEM:
            if (I18n_OnDrawItem(hwnd, (const DRAWITEMSTRUCT*)lp)) return TRUE;
            break;
        case WM_MENUCHAR: {
            /* owner-draw 菜单的助记符导航（Alt+F / 菜单打开后按字母） */
            int pos = I18n_FindMnemonic((HMENU)lp, LOWORD(wp));
            if (pos >= 0) return MAKELRESULT(pos, MNC_EXECUTE);
            break;
        }
        case WM_CONTEXTMENU: {
            /* 右键位置在标签栏 → 标签菜单；在编辑区 → 剪贴板菜单；其余忽略 */
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            if (pt.x == -1 && pt.y == -1) return 0;   /* 键盘菜单键：忽略 */
            RECT rcTab; GetWindowRect(g_hwndTab, &rcTab);
            if (PtInRect(&rcTab, pt)) {
                POINT tpt = pt; ScreenToClient(g_hwndTab, &tpt);
                BOOL closeHit; int arrow;
                int ti = TabBar_HitTestPublic(g_hwndTab, tpt.x, tpt.y, &closeHit, &arrow);
                ShowTabContextMenu(hwnd, pt, ti);
                return 0;
            }
            ShowEditContextMenu(hwnd, pt);
            return 0;
        }
        case WM_ACTIVATEAPP:
            /* 窗口重新获得焦点：活动文档是"干净的已保存文件"且磁盘已变 → 提示重新加载。
               脏文档不自动提示（保护未保存修改，保存时另有覆盖检查）。 */
            if (wp && g_curDoc >= 0) {
                Doc* d = &g_docs[g_curDoc];
                if (!d->isNew && !d->dirty && d->path[0]) {
                    FILETIME cur;
                    if (DiskWriteTimePublic(d->path, &cur) &&
                        CompareFileTime(&cur, &d->ftWrite) != 0) {
                        wchar_t msg[400];
                        wsprintf(msg, T(STR_MSG_FILE_CHANGED_RELOAD), d->title);
                        if (MessageBoxW(hwnd, msg, T(STR_APP_TITLE),
                                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            Editor_LoadFile(g_curDoc, d->path, d->enc);
                            Editor_UpdateStatus();
                        } else {
                            d->ftWrite = cur;   /* 不再重复询问 */
                        }
                    }
                }
            }
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                if (g_draftsDirty) Session_SaveDrafts();
                /* 正式文件未保存修改的定时备份（崩溃后可恢复） */
                for (int i = 0; i < g_docCount; i++) {
                    if (g_docs[i].dirty && !g_docs[i].isNew)
                        Session_AutoBackupWrite(i);
                }
            }
            return 0;
        case WM_CLOSE:
            if (ConfirmExit()) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            Session_SaveDrafts();
            Session_Save();
            Settings_Save();
            Session_SaveWindow(hwnd);
            KillTimer(hwnd, 1);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------- entry ---------------- */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)cmdLine; (void)hPrev;
    g_hInst = hInst;
    I18n_Init();   /* 读用户上次的界面语言选择（无记录则跟随系统语言） */
    Settings_Load();   /* 主题/换行/行号/字号（建窗前加载，建窗时生效） */
    I18n_ApplySystemThemeMode();   /* 须在 g_dark 就绪后：系统箭头/消息框明暗跟随 */
    g_dpi = GetDpiForSystem();   /* 窗口创建前先取系统 DPI，建窗后再按所在显示器校正 */
    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"etonClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"etonClass", L"ETON",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, UI_Scale(900), UI_Scale(640),
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    {
        UINT dpi = GetDpiForWindow(hwnd);   /* 窗口实际落在的显示器可能与系统 DPI 不同 */
        if (dpi && dpi != g_dpi) { g_dpi = dpi; Editor_OnDpiChanged(); }
    }
    ShowWindow(hwnd, Session_RestoreWindow(hwnd, nShow));   /* 恢复上次窗口位置,首次运行居中 */
    UpdateWindow(hwnd);

    HACCEL accel = LoadAcceleratorsW(hInst, MAKEINTRESOURCEW(IDR_ACCEL));
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        /* 非模态查找/替换对话框需要 IsDialogMessage 处理 Tab/Enter/Esc 导航 */
        if (g_hFindDlg && IsDialogMessageW(g_hFindDlg, &msg)) continue;
        if (!TranslateAcceleratorW(hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
