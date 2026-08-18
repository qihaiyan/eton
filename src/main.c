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
    MessageBoxW(NULL, msg, L"eton", MB_ICONERROR);
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
    HMENU bar = GetMenu(g_hwndMain);
    if (!bar) return;
    HMENU file = GetSubMenu(bar, 0);
    if (!file) return;
    int n = GetMenuItemCount(file);
    int idx = -1;
    for (int i = 0; i < n; i++) {
        wchar_t buf[32]; buf[0] = L'\0';
        if (GetMenuStringW(file, i, buf, 31, MF_BYPOSITION) && wcsstr(buf, L"最近文件")) { idx = i; break; }
    }
    if (idx < 0) return;
    HMENU sub = GetSubMenu(file, idx);
    if (!sub) return;
    int cnt;
    for (int guard = 0; guard < 256 && (cnt = GetMenuItemCount(sub)) > 0; guard++)
        RemoveMenu(sub, MF_BYPOSITION, cnt - 1);
    if (g_recentCount == 0) {
        AppendMenuW(sub, MF_STRING | MF_GRAYED, IDM_RECENT_FIRST, L"(空)");
    } else {
        for (int i = 0; i < g_recentCount; i++)
            AppendMenuW(sub, MF_STRING, IDM_RECENT_FIRST + i, g_recent[i]);
    }
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
    Encoding e = (g_curDoc >= 0) ? g_docs[g_curDoc].enc : ENC_UTF8;
    LangID lg = (g_curDoc >= 0) ? g_docs[g_curDoc].lang : LANG_NONE;
    int eol = (g_curDoc >= 0) ? g_docs[g_curDoc].eol : 0;
    for (int i = 0; i <= 4; i++) Check(m, IDM_ENC_ANSI + i, (Encoding)i == e);
    for (int i = 0; i <= 4; i++) Check(m, IDM_REOPEN_ANSI + i, FALSE);
    for (int i = 0; i <= 2; i++) Check(m, IDM_EOL_CRLF + i, i == eol);
    for (int i = 0; i <= (IDM_LANG_LAST - IDM_LANG_FIRST); i++)
        Check(m, IDM_LANG_NONE + i, (LangID)i == lg);
}

/* ---------------- file open helpers ---------------- */
static void OpenFiles(HWND hwnd) {
    wchar_t buf[32768]; memset(buf, 0, sizeof(buf));
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0文本文件 (*.txt;*.c;*.cpp;*.h;*.py;*.js;*.json;*.xml;*.html;*.sql)\0*.txt;*.c;*.cpp;*.h;*.py;*.js;*.json;*.xml;*.html;*.sql\0";
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
    Sci_Position curStart = (Sci_Position)SendMessage(edit, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position selStart = (Sci_Position)SendMessage(edit, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = (Sci_Position)SendMessage(edit, SCI_GETSELECTIONEND, 0, 0);
    Sci_Position start = down ? selEnd : selStart;
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
    SendMessage(edit, SCI_SETSEARCHFLAGS, flags, 0);
    Sci_Position pos = (Sci_Position)SendMessage(edit, SCI_SEARCHINTARGET, (WPARAM)len, (LPARAM)utf8);
    if (pos >= 0) {
        SendMessage(edit, SCI_SETSEL, pos, pos + len);
        SendMessage(edit, SCI_SCROLLCARET, 0, 0);
    } else {
        /* wrap */
        if (down) {
            SendMessage(edit, SCI_SETTARGETSTART, 0, 0);
            SendMessage(edit, SCI_SETTARGETEND, start, 0);
        } else {
            SendMessage(edit, SCI_SETTARGETSTART, (WPARAM)SendMessage(edit, SCI_GETLENGTH, 0, 0), 0);
            SendMessage(edit, SCI_SETTARGETEND, start, 0);
        }
        pos = (Sci_Position)SendMessage(edit, SCI_SEARCHINTARGET, (WPARAM)len, (LPARAM)utf8);
        if (pos >= 0) {
            SendMessage(edit, SCI_SETSEL, pos, pos + len);
            SendMessage(edit, SCI_SCROLLCARET, 0, 0);
        } else {
            MessageBoxW(hwnd, L"未找到匹配项。", L"查找", MB_OK | MB_ICONINFORMATION);
        }
    }
}

static void ResizeStatus(void) {
    RECT rc; GetClientRect(g_hwndMain, &rc);
    int cx = rc.right;
    int p[5];
    p[0] = cx * 45 / 100; p[1] = cx * 65 / 100; p[2] = cx * 78 / 100; p[3] = cx * 90 / 100; p[4] = -1;
    SendMessage(g_hwndStatus, SB_SETPARTS, 5, (LPARAM)p);
}

static BOOL ConfirmExit(void) {
    for (int i = 0; i < g_docCount; i++) {
        if (g_docs[i].dirty && !g_docs[i].isNew) {   /* 草稿自动保存，无需确认 */
            wchar_t msg[300];
            wsprintf(msg, L"文件 \"%s\" 尚未保存，是否保存？", g_docs[i].title);
            int r = MessageBoxW(g_hwndMain, msg, L"eton", MB_YESNOCANCEL | MB_ICONQUESTION);
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
        case IDM_NEW: { int ni = Editor_NewDoc(NULL, L"未命名", TRUE); if (ni >= 0) Editor_Activate(ni); } break;
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
        case IDM_THEME:
            g_dark = !g_dark; Editor_ApplyThemeColors();
            for (int i = 0; i < g_docCount; i++) Editor_ApplyTheme(i);
            InvalidateRect(g_hwndTab, NULL, FALSE);
            UpdateMenuChecks(); break;
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
            if (g_curDoc >= 0) { Editor_SetLang(g_curDoc, (LangID)(id - IDM_LANG_NONE)); UpdateMenuChecks(); } break;
        case IDM_FINDNEXT: FindNextAccel(hwnd, TRUE); break;
        case IDM_FINDPREV: FindNextAccel(hwnd, FALSE); break;
        case IDM_ABOUT: Dlg_About(hwnd); break;
        default:
            if (id >= IDM_RECENT_FIRST && id <= IDM_RECENT_LAST) OpenRecent(id - IDM_RECENT_FIRST);
            break;
    }
    return 0;
}

/* ---------------- window proc ---------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(icex); icex.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);
            Editor_Init();
            SetMenu(hwnd, LoadMenuW(g_hInst, MAKEINTRESOURCEW(IDR_MENU_MAIN)));
            g_hwndTab = CreateWindowExW(0, L"NPPTabBar", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            ResizeStatus();
            DragAcceptFiles(hwnd, TRUE);   /* 接收从资源管理器拖入的文件 */

            /* open file from command line */
            BOOL haveArg = FALSE;
            wchar_t* cl = GetCommandLineW();
            while (*cl && *cl != L' ') { if (*cl == L'"') { cl++; while (*cl && *cl != L'"') cl++; if (*cl) cl++; } else cl++; }
            while (*cl == L' ') cl++;
            if (*cl) {
                wchar_t path[MAX_PATH];
                if (*cl == L'"') { cl++; int i = 0; while (*cl && *cl != L'"' && i < MAX_PATH - 1) path[i++] = *cl++; path[i] = 0; }
                else { int i = 0; while (*cl && *cl != L' ' && i < MAX_PATH - 1) path[i++] = *cl++; path[i] = 0; }
                if (path[0]) OpenFileByPath(path);
                haveArg = (g_docCount > 0);
            }

            /* 无命令行参数时恢复上次会话；两者都没打开文件才新建空文档 */
            if (!haveArg && Session_Restore() == 0) {
                int ni = Editor_NewDoc(NULL, L"未命名", TRUE);
                Editor_Activate(ni);
            }
            SetTimer(hwnd, 1, 10000, NULL);   /* 定时落盘未命名文档草稿 */
            RefreshRecentMenu();
            return 0;
        }
        case WM_SIZE:
            ResizeStatus();
            Editor_Layout();
            return 0;
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
        case WM_TIMER:
            if (wp == 1 && g_draftsDirty) Session_SaveDrafts();
            return 0;
        case WM_CLOSE:
            if (ConfirmExit()) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            Session_SaveDrafts();
            Session_Save();
            KillTimer(hwnd, 1);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------- entry ---------------- */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)cmdLine; (void)hPrev;
    g_hInst = hInst;
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

    HWND hwnd = CreateWindowExW(0, L"etonClass", L"eton",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 640,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    HACCEL accel = LoadAcceleratorsW(hInst, MAKEINTRESOURCEW(IDR_ACCEL));
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
