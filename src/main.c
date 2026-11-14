#include "common.h"
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <stdarg.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
HINSTANCE g_hInst;
HWND g_hwndMain, g_hwndTab, g_hwndTool, g_hwndStatus;
UINT g_dpi = 96;
int g_editorDWrite = 1;
#define ETON_COPYDATA_MAGIC 0x45544F4Eu
int UI_Scale(int px) {
    return MulDiv(px, (int)g_dpi, 96);
}
Doc g_docs[MAX_DOCS];
int g_docCount = 0;
int g_curDoc = -1;
HFONT g_hFont = NULL;
int g_fontGen = 0;
int g_fontSize = 11;
BOOL g_wordWrap = FALSE;
BOOL g_showGutter = TRUE;
BOOL g_dark = TRUE;
int g_gutterWidth = 40;
wchar_t g_recent[MAX_RECENT][MAX_PATH];
int g_recentCount = 0;
COLORREF g_clrBg, g_clrFg, g_clrSelBg, g_clrGutterBg, g_clrGutterFg, g_clrStatusBg, g_clrStatusFg;
BOOL g_suppressDirty = FALSE;
BOOL g_draftsDirty = FALSE;
BOOL g_sessionDirty = FALSE;
BOOL g_mdSplit = FALSE;
BOOL g_mdLiveEdit = FALSE;
BOOL g_mdSyncLock = FALSE;
BOOL g_mdOutline = TRUE;
BOOL g_fileTree = TRUE;
void ShowError(const wchar_t* msg) {
    MessageBoxW(NULL, msg, T(STR_APP_TITLE), MB_ICONERROR);
}
int WFmtV(wchar_t* buf, int cch, const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(buf, cch, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= cch) buf[cch - 1] = L'\0';
    return n;
}
void ApplyTitleBarTheme(HWND hwnd) {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT (WINAPI *PFN)(HWND, DWORD, LPCVOID, DWORD);
    PFN p = (PFN)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!p) return;
    BOOL v = g_dark ? TRUE : FALSE;
    p(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
    InvalidateRect(hwnd, NULL, TRUE);
    RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
}
static HMENU g_hMenuRoot = NULL;
static int  s_capHover = -1;
static HHOOK s_menuHook = NULL;
static int  s_menuOpenIdx = -1;
static int  s_menuSwitchTo = -1;
static int  s_capPress = -1;
static BOOL s_capTrack = FALSE;
static void CapInvalidate(HWND hwnd, int idx);
void CaptionButton_ClearHover(void) {
    if (s_capHover < 0) return;
    s_capHover = -1;
    RECT rc; GetClientRect(g_hwndMain, &rc);
    RECT zone = { rc.right - CaptionButtonsWidth(), 0, rc.right, UI_Scale(32) };
    InvalidateRect(g_hwndMain, &zone, FALSE);
    UpdateWindow(g_hwndMain);
}
int CaptionButtonsWidth(void) {
    return 3 * UI_Scale(46);
}
static void CapRects(HWND hwnd, RECT* out) {
    RECT rc; GetClientRect(hwnd, &rc);
    int w = UI_Scale(46);
    int x = rc.right - 3 * w;
    for (int i = 0; i < 3; i++) {
        out[i].left = x; out[i].right = x + w;
        out[i].top = 0; out[i].bottom = UI_Scale(30);
        x += w;
    }
}
static void CapInvalidate(HWND hwnd, int idx) {
    if (idx < 0) return;
    RECT rs[3]; CapRects(hwnd, rs);
    InvalidateRect(hwnd, &rs[idx], FALSE);
}
static void CapPaint(HWND hwnd, HDC hdc) {
    RECT rs[3]; CapRects(hwnd, rs);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    BOOL act = (GetActiveWindow() == hwnd);
    for (int i = 0; i < 3; i++) {
        HBRUSH bg = CreateSolidBrush(g_dark ? RGB(37, 37, 38) : RGB(225, 225, 228));
        FillRect(hdc, &rs[i], bg);
        DeleteObject(bg);
        if (i == s_capHover && i == 2)
            bg = CreateSolidBrush(RGB(232, 17, 35));
        else if (i == s_capHover)
            bg = CreateSolidBrush(g_dark ? RGB(70, 70, 74) : RGB(229, 229, 231));
        if (bg) { FillRect(hdc, &rs[i], bg); DeleteObject(bg); }
        COLORREF fg = act ? (g_dark ? RGB(236, 236, 236) : RGB(20, 20, 20))
                          : (g_dark ? RGB(160, 160, 160) : RGB(160, 160, 160));
        HPEN pen = CreatePen(PS_SOLID, UI_Scale(1) < 1 ? 1 : UI_Scale(1), fg);
        HGDIOBJ op = SelectObject(hdc, pen);
        int cx = (rs[i].left + rs[i].right) / 2;
        int cy = (rs[i].top + rs[i].bottom) / 2;
        int r = UI_Scale(4);
        if (i == 0) {
            MoveToEx(hdc, cx - r, cy, NULL); LineTo(hdc, cx + r + 1, cy);
        } else if (i == 1) {
            if (IsZoomed(hwnd)) {
                RECT a = {cx - r + UI_Scale(2), cy - r, cx + r, cy + r - UI_Scale(2)};
                RECT b = {cx - r, cy - r + UI_Scale(2), cx + r - UI_Scale(2), cy + r};
                Rectangle(hdc, b.left, b.top, b.right, b.bottom);
                Rectangle(hdc, a.left, a.top, a.right, a.bottom);
            } else {
                Rectangle(hdc, cx - r, cy - r, cx + r, cy + r);
            }
        } else {
            MoveToEx(hdc, cx - r, cy - r, NULL); LineTo(hdc, cx + r, cy + r);
            MoveToEx(hdc, cx + r, cy - r, NULL); LineTo(hdc, cx - r, cy + r);
        }
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
    SelectObject(hdc, ob);
}
static int CapHit(HWND hwnd, int x, int y) {
    RECT rs[3]; CapRects(hwnd, rs);
    for (int i = 0; i < 3; i++)
        if (x >= rs[i].left && x < rs[i].right && y >= rs[i].top && y < rs[i].bottom)
            return i;
    return -1;
}
static LRESULT CALLBACK MenuStripHook(int code, WPARAM wp, LPARAM lp) {
    if (code == MSGF_MENU && s_menuOpenIdx >= 0) {
        MSG* m = (MSG*)lp;
        if (m->message == WM_MOUSEMOVE || m->message == WM_NCLBUTTONDOWN ||
            m->message == WM_LBUTTONDOWN) {
            DWORD pos = GetMessagePos();
            POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
            int hit = Toolbar_MenuHit(pt);
            if (hit >= 0 && hit != s_menuOpenIdx) {
                s_menuSwitchTo = hit;
                PostMessage(g_hwndMain, WM_CANCELMODE, 0, 0);
            }
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}
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
static void RebuildMainMenu(HWND hwnd) {
    (void)hwnd;
    if (g_hMenuRoot) DestroyMenu(g_hMenuRoot);
    g_hMenuRoot = I18n_BuildMainMenu();
    RefreshRecentMenu();
}
static void Check(HMENU m, UINT id, BOOL on) {
    CheckMenuItem(m, id, on ? MF_CHECKED : MF_UNCHECKED);
}
static void UpdateMenuChecksOn(HMENU m) {
    if (!m) return;
    Check(m, IDM_WRAP, g_wordWrap);
    Check(m, IDM_GUTTER, g_showGutter);
    Check(m, IDM_THEME, g_dark);
    Check(m, IDM_VIEW_DW, g_editorDWrite);
    Check(m, IDM_VIEW_FILETREE, g_fileTree);
    BOOL isMd = (g_curDoc >= 0 && g_docs[g_curDoc].lang == LANG_MD);
    Check(m, IDM_VIEW_MD, isMd && g_docs[g_curDoc].previewOn);
    EnableMenuItem(m, IDM_VIEW_MD, isMd ? MF_BYCOMMAND | MF_ENABLED
                                        : MF_BYCOMMAND | MF_GRAYED);
    Check(m, IDM_VIEW_MDSPLT, isMd && g_mdSplit);
    EnableMenuItem(m, IDM_VIEW_MDSPLT, isMd ? MF_BYCOMMAND | MF_ENABLED
                                            : MF_BYCOMMAND | MF_GRAYED);
    Check(m, IDM_VIEW_MDLIVE, isMd && g_mdLiveEdit);
    EnableMenuItem(m, IDM_VIEW_MDLIVE, isMd ? MF_BYCOMMAND | MF_ENABLED
                                            : MF_BYCOMMAND | MF_GRAYED);
    Check(m, IDM_VIEW_MDOUTLINE, isMd && g_mdOutline);
    EnableMenuItem(m, IDM_VIEW_MDOUTLINE, isMd ? MF_BYCOMMAND | MF_ENABLED
                                               : MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(m, IDM_EXPORT_HTML, isMd ? MF_BYCOMMAND | MF_ENABLED
                                            : MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(m, IDM_EXPORT_PDF, isMd ? MF_BYCOMMAND | MF_ENABLED
                                           : MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(m, IDM_EXPORT_DOCX, isMd ? MF_BYCOMMAND | MF_ENABLED
                : MF_BYCOMMAND | MF_GRAYED);
    LangID lg = (g_curDoc >= 0) ? g_docs[g_curDoc].lang : LANG_NONE;
    int eol = (g_curDoc >= 0) ? g_docs[g_curDoc].eol : 0;
    for (int i = 0; i <= 2; i++) Check(m, IDM_EOL_CRLF + i, i == eol);
    for (int i = 0; i <= (IDM_LANG_LAST - IDM_LANG_FIRST); i++)
        Check(m, IDM_LANG_NONE + i, (LangID)i == lg);
    Check(m, IDM_UI_LANG_ZH, I18n_Lang() == UI_LANG_ZH);
    Check(m, IDM_UI_LANG_EN, I18n_Lang() == UI_LANG_EN);
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
static void UpdateMenuChecks(void) {
    if (!g_hMenuRoot) return;
    int n = GetMenuItemCount(g_hMenuRoot);
    for (int i = 0; i < n; i++)
        UpdateMenuChecksOn(GetSubMenu(g_hMenuRoot, i));
}
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
static void InsertDateTime(void) {
    HWND edit = Editor_ActiveEdit();
    if (!edit) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[64];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SendMessage(edit, SCI_REPLACESEL, 0, (LPARAM)buf);
}
static void FindNextAccel(HWND hwnd, BOOL down) {
    HWND edit = Editor_ActiveEdit();
    if (!edit) return;
    if (wcslen(g_findText) == 0) { Dlg_Find(hwnd, FALSE); return; }
    char utf8[2048];
    Sci_Position len = Find_TextToUtf8(g_findText, utf8, sizeof(utf8));
    if (len <= 0) return;
    Sci_Position curStart = (Sci_Position)SendMessage(edit, SCI_GETCURRENTPOS, 0, 0);
    Sci_Position selStart = (Sci_Position)SendMessage(edit, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = (Sci_Position)SendMessage(edit, SCI_GETSELECTIONEND, 0, 0);
    Sci_Position start = down ? selEnd : selStart;
    (void)curStart;
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
        if (g_docs[i].dirty && !g_docs[i].isNew && !g_docs[i].bakRestored) {
            wchar_t msg[300];
            WFmt(msg, T(STR_MSG_FILE_NOT_SAVED), g_docs[i].title);
            int r = MessageBoxW(g_hwndMain, msg, T(STR_APP_TITLE), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return FALSE;
            if (r == IDYES) { if (!Editor_SaveDoc(i, FALSE)) return FALSE; }
        }
    }
    return TRUE;
}
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
        case IDM_DT_B64_ENC:   DevTools_TransformActiveDoc(DT_B64_ENC);   break;
        case IDM_DT_B64_DEC:   DevTools_TransformActiveDoc(DT_B64_DEC);   break;
        case IDM_DT_URL_ENC:   DevTools_TransformActiveDoc(DT_URL_ENC);   break;
        case IDM_DT_URL_DEC:   DevTools_TransformActiveDoc(DT_URL_DEC);   break;
        case IDM_DT_UNI_ESC:   DevTools_TransformActiveDoc(DT_UNI_ESC);   break;
        case IDM_DT_UNI_UNESC: DevTools_TransformActiveDoc(DT_UNI_UNESC); break;
        case IDM_DT_HASH: DevTools_ShowHashDlg(hwnd); break;
        case IDM_DT_UUID: DevTools_InsertUuid(); break;
        case IDM_DT_TS:   DevTools_ShowTsDlg(hwnd); break;
        case IDM_WRAP:
            g_wordWrap = !g_wordWrap;
            if (g_curDoc >= 0) Editor_ApplyWrap(g_curDoc);
            UpdateMenuChecks(); break;
        case IDM_GUTTER:
            g_showGutter = !g_showGutter;
            if (g_curDoc >= 0) {
                Editor_ComputeGutterWidth(g_curDoc);
                Editor_Layout();
            }
            UpdateMenuChecks(); break;
        case IDM_ZOOMIN: Editor_Zoom(1); break;
        case IDM_ZOOMOUT: Editor_Zoom(-1); break;
        case IDM_ZOOMRST: g_fontSize = 11; Editor_SetFont(); Editor_UpdateStatus(); break;
        case IDM_THEME: {
            g_dark = !g_dark; Editor_ApplyThemeColors();
            I18n_ApplySystemThemeMode();
            for (int i = 0; i < g_docCount; i++) Editor_ApplyTheme(i);
            MdView_OnThemeChange();
            MdOutline_OnThemeChange();
            FileTree_OnThemeChange();
            ApplyTitleBarTheme(hwnd);
            InvalidateRect(g_hwndTab, NULL, FALSE);
            InvalidateRect(g_hwndTool, NULL, FALSE);
            InvalidateRect(g_hwndStatus, NULL, FALSE);
            InvalidateRect(hwnd, NULL, TRUE);
            ScrollBars_Repaint();
            TabBar_ApplyTheme();
            RebuildMainMenu(hwnd);
            UpdateMenuChecks();
            break;
        }
        case IDM_VIEW_DW:
            g_editorDWrite = !g_editorDWrite;
            Editor_ApplyTextRendering();
            UpdateMenuChecks();
            break;
        case IDM_EOL_CRLF: case IDM_EOL_LF: case IDM_EOL_CR:
            if (g_curDoc >= 0) { g_docs[g_curDoc].eol = id - IDM_EOL_CRLF; Editor_UpdateStatus(); UpdateMenuChecks(); } break;
        case IDM_EOL_CONVERT: if (g_curDoc >= 0) Editor_ConvertEol(g_curDoc, g_docs[g_curDoc].eol); break;
        case IDM_LANG_NONE: case IDM_LANG_C: case IDM_LANG_CPP: case IDM_LANG_CS: case IDM_LANG_JAVA:
        case IDM_LANG_JS: case IDM_LANG_PY: case IDM_LANG_XML: case IDM_LANG_JSON: case IDM_LANG_SQL:
        case IDM_LANG_MD: case IDM_LANG_YAML:
            if (g_curDoc >= 0) { Editor_SetLang(g_curDoc, (LangID)(id - IDM_LANG_NONE)); UpdateMenuChecks(); } break;
        case IDM_VIEW_MD:
            MdView_Toggle();
            break;
        case IDM_VIEW_MDSPLT:
            MdView_ToggleSplit();
            break;
        case IDM_VIEW_MDLIVE:
            g_mdLiveEdit = !g_mdLiveEdit;
            if (!g_mdLiveEdit && MdEdit_Active()) MdEdit_Commit(TRUE);
            UpdateMenuChecks();
            break;
        case IDM_VIEW_MDOUTLINE:
            MdOutline_Toggle();
            UpdateMenuChecks();
            break;
        case IDM_VIEW_FILETREE:
            FileTree_Toggle();
            UpdateMenuChecks();
            break;
        case IDM_EXPORT_HTML:
            MdExport_Html();
            break;
        case IDM_EXPORT_PDF:
            MdExport_Pdf();
            break;
        case IDM_EXPORT_DOCX:
            MdExport_Docx();
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
            for (int i = g_docCount - 1; i >= 0; i--)
                if (i != g_curDoc) Editor_CloseDoc(i);
            break;
        case IDM_CLOSE_ALL:
            while (g_docCount > 1) Editor_CloseDoc(g_docCount - 1);
            break;
        case IDM_BM_TOGGLE: if (g_curDoc >= 0) Editor_ToggleBookmark(g_curDoc); break;
        case IDM_BM_NEXT:   if (g_curDoc >= 0) Editor_GotoBookmark(g_curDoc, TRUE); break;
        case IDM_BM_PREV:   if (g_curDoc >= 0) Editor_GotoBookmark(g_curDoc, FALSE); break;
        case IDM_EXPLORER:
            if (g_curDoc >= 0 && !g_docs[g_curDoc].isNew && g_docs[g_curDoc].path[0]) {
                wchar_t params[MAX_PATH + 16];
                WFmt(params, L"/select,\"%s\"", g_docs[g_curDoc].path);
                ShellExecuteW(hwnd, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
            }
            break;
        case IDM_FILEDIFF:
            FileDiff_Run();
            break;
        case IDM_UI_LANG_ZH: case IDM_UI_LANG_EN:
            if (I18n_Lang() != id - IDM_UI_LANG_ZH) {
                I18n_SetLang(id - IDM_UI_LANG_ZH);
                if (g_hFindDlg) { DestroyWindow(g_hFindDlg); g_hFindDlg = NULL; }
                RebuildMainMenu(hwnd);
                UpdateMenuChecks();
                Editor_OnLanguageChanged();
                MdOutline_OnThemeChange();
                FileTree_OnThemeChange();
                InvalidateRect(g_hwndTab, NULL, FALSE);
            }
            break;
        default:
            if (id >= IDM_RECENT_FIRST && id <= IDM_RECENT_LAST) OpenRecent(id - IDM_RECENT_FIRST);
            break;
    }
    return 0;
}
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
    I18n_OwnerAppend(m, MF_OWNERDRAW, 0, NULL);
    I18n_OwnerAppend(m, on, IDM_SELECTALL, T(STR_ITEM_SELECTALL));
    I18n_OwnerAppend(m, MF_OWNERDRAW, 0, NULL);
    if (g_curDoc >= 0 && !g_docs[g_curDoc].isNew && g_docs[g_curDoc].path[0])
        I18n_OwnerAppend(m, on, IDM_EXPLORER, T(STR_ITEM_EXPLORER));
    I18n_ApplyMenuTheme(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (cmd) PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}
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
        if (tabIndex >= 0) PostMessage(hwnd, WM_APP_TABSEL, (WPARAM)tabIndex, 0);
        PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDM_CLOSE_OTHER, 0), 0);
    } else if (cmd == IDM_CLOSE_ALL) {
        PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDM_CLOSE_ALL, 0), 0);
    }
}
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
            InitCommonControlsEx(&icex);
            ScrollBars_Create(hwnd);
            Editor_Init();
            g_hMenuRoot = I18n_BuildMainMenu();
            RefreshRecentMenu();
            Toolbar_Register();
            g_hwndTool = CreateWindowExW(0, L"ETONToolbar", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            g_hwndTab = CreateWindowExW(0, L"NPPTabBar", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            StatusBar_Register();
            g_hwndStatus = CreateWindowExW(0, L"ETONStatus", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
            ResizeStatus();
            DragAcceptFiles(hwnd, TRUE);
            int argc = 0;
            wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            Session_Restore();
            if (argv) {
                for (int i = 1; i < argc; i++)
                    if (argv[i][0]) OpenFileByPath(argv[i]);
                LocalFree(argv);
            }
            if (g_docCount == 0) {
                int ni = Editor_NewDoc(NULL, T(STR_UNTITLED), TRUE);
                Editor_Activate(ni);
            }
            Editor_PrewarmRender();
            SetTimer(hwnd, 1, 10000, NULL);
            ApplyTitleBarTheme(hwnd);
            RefreshRecentMenu();
            return 0;
        }
        case WM_SIZE:
            ResizeStatus();
            Editor_Layout();
            return 0;
        case WM_NCCALCSIZE: {
            if (!wp) break;
            NCCALCSIZE_PARAMS* ncp = (NCCALCSIZE_PARAMS*)lp;
            RECT wr = ncp->rgrc[0];
            DefWindowProcW(hwnd, msg, wp, lp);
            int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            ncp->rgrc[0].left = wr.left + fx;
            ncp->rgrc[0].top = wr.top + fy;
            ncp->rgrc[0].right = wr.right - fx;
            ncp->rgrc[0].bottom = wr.bottom - fy;
            return 0;
        }
        case WM_NCHITTEST: {
            LRESULT ht = DefWindowProcW(hwnd, msg, wp, lp);
            if (ht == HTCLIENT) {
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                RECT wl; GetWindowRect(hwnd, &wl);
                int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                x -= wl.left; y -= wl.top;
                if (!IsZoomed(hwnd) && y < fy) {
                    if (x < fx) return HTTOPLEFT;
                    if (x >= wl.right - wl.left - fx) return HTTOPRIGHT;
                    return HTTOP;
                }
                RECT rc; GetClientRect(hwnd, &rc);
                POINT pt = { x, y };
                ScreenToClient(hwnd, &pt);
                if (pt.y < UI_Scale(30) && pt.x >= rc.right - CaptionButtonsWidth()) {
                    if (CapHit(hwnd, pt.x, pt.y) >= 0) return HTCLIENT;
                    return HTCAPTION;
                }
            }
            return ht;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            CapPaint(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int h = CapHit(hwnd, x, y);
            if (h != s_capHover) {
                s_capHover = h;
                RECT rc; GetClientRect(hwnd, &rc);
                RECT zone = { rc.right - CaptionButtonsWidth(), 0, rc.right, UI_Scale(32) };
                InvalidateRect(hwnd, &zone, FALSE);
                UpdateWindow(hwnd);
            }
            if (!s_capTrack) {
                TRACKMOUSEEVENT tme; memset(&tme, 0, sizeof(tme));
                tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                s_capTrack = TRUE;
            }
            break;
        }
        case WM_MOUSELEAVE:
            s_capTrack = FALSE;
            CaptionButton_ClearHover();
            break;
        case WM_NCMOUSEMOVE:
            CaptionButton_ClearHover();
            break;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int h = CapHit(hwnd, x, y);
            if (h >= 0) s_capPress = h;
            break;
        }
        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int h = CapHit(hwnd, x, y);
            if (h >= 0 && h == s_capPress) {
                if (h == 0) ShowWindow(hwnd, SW_MINIMIZE);
                else if (h == 1) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                else PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            s_capPress = -1;
            break;
        }
        case WM_ERASEBKGND: {
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH hb = CreateSolidBrush(g_clrBg);
            FillRect((HDC)wp, &rc, hb);
            DeleteObject(hb);
            return 1;
        }
        case WM_DPICHANGED: {
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
        case WM_COPYDATA: {
            const COPYDATASTRUCT* cds = (const COPYDATASTRUCT*)lp;
            if (cds && cds->dwData == ETON_COPYDATA_MAGIC &&
                cds->cbData >= 2 * sizeof(DWORD) && cds->lpData) {
                const DWORD* hdr = (const DWORD*)cds->lpData;
                DWORD count = hdr[1];
                const wchar_t* p =
                    (const wchar_t*)((const BYTE*)cds->lpData + 2 * sizeof(DWORD));
                const wchar_t* end =
                    (const wchar_t*)((const BYTE*)cds->lpData + cds->cbData);
                for (DWORD i = 0; i < count && p < end; i++) {
                    const wchar_t* q = p;
                    while (q < end && *q) q++;
                    if (q >= end) break;
                    if (*p) OpenFileByPath(p);
                    p = q + 1;
                }
                if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                return TRUE;
            }
            break;
        }
        case WM_DROPFILES: {
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
            if (MdEdit_OnNotify(lp)) return 0;
            Editor_OnNotify(lp);
            return 0;
        case WM_APP_TABSEL:
            Editor_Activate((int)wp); return 0;
        case WM_APP_TABCLOSE:
            Editor_CloseDoc((int)wp); return 0;
        case WM_INITMENUPOPUP:
            UpdateMenuChecks();
            return 0;
        case WM_APP_STRIP_MENU: {
            if (!g_hMenuRoot) break;
            UpdateMenuChecks();
            int mx = (int)wp;
            int my = (int)lp;
            if (mx == 0 && my == 0) {
                RECT rc; GetWindowRect(g_hwndTool, &rc);
                mx = rc.left + UI_Scale(15);
                my = rc.bottom + 1;
            }
            int cmd = TrackPopupMenu(g_hMenuRoot, TPM_RETURNCMD | TPM_LEFTBUTTON,
                                     mx, my, 0, hwnd, NULL);
            if (cmd) PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
            return 0;
        }
        case WM_APP_STRIP_GROUP: {
            if (!g_hMenuRoot) break;
            int idx = (int)wp;
            int mx = (int)(short)LOWORD(lp);
            int my = (int)(short)HIWORD(lp);
            for (;;) {
                HMENU sub = GetSubMenu(g_hMenuRoot, idx);
                if (!sub) break;
                UpdateMenuChecks();
                s_menuOpenIdx = idx;
                s_menuSwitchTo = -1;
                s_menuHook = SetWindowsHookExW(WH_MSGFILTER, MenuStripHook,
                                               NULL, GetCurrentThreadId());
                int cmd = TrackPopupMenu(sub, TPM_RETURNCMD | TPM_LEFTBUTTON,
                                         mx, my, 0, hwnd, NULL);
                if (s_menuHook) { UnhookWindowsHookEx(s_menuHook); s_menuHook = NULL; }
                s_menuOpenIdx = -1;
                if (cmd) {
                    PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
                    break;
                }
                if (s_menuSwitchTo >= 0) {
                    idx = s_menuSwitchTo;
                    s_menuSwitchTo = -1;
                    RECT rcm;
                    if (Toolbar_MenuItemRect(idx, &rcm)) {
                        mx = (rcm.left + rcm.right) / 2;
                        my = rcm.bottom + 1;
                    }
                    continue;
                }
                break;
            }
            return 0;
        }
        case WM_MEASUREITEM:
            if (I18n_OnMeasureItem(hwnd, (MEASUREITEMSTRUCT*)lp)) return TRUE;
            break;
        case WM_DRAWITEM:
            if (I18n_OnDrawItem(hwnd, (const DRAWITEMSTRUCT*)lp)) return TRUE;
            break;
        case WM_MENUCHAR: {
            int pos = I18n_FindMnemonic((HMENU)lp, LOWORD(wp));
            if (pos >= 0) return MAKELRESULT(pos, MNC_EXECUTE);
            break;
        }
        case WM_CONTEXTMENU: {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            if (pt.x == -1 && pt.y == -1) return 0;
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
            if (wp && g_curDoc >= 0) {
                Doc* d = &g_docs[g_curDoc];
                if (!d->isNew && !d->dirty && d->path[0]) {
                    FILETIME cur;
                    if (DiskWriteTimePublic(d->path, &cur) &&
                        CompareFileTime(&cur, &d->ftWrite) != 0) {
                        wchar_t msg[400];
                        WFmt(msg, T(STR_MSG_FILE_CHANGED_RELOAD), d->title);
                        if (MessageBoxW(hwnd, msg, T(STR_APP_TITLE),
                                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            Editor_LoadFile(g_curDoc, d->path, d->enc);
                            Editor_UpdateStatus();
                        } else {
                            d->ftWrite = cur;
                        }
                    }
                }
            }
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                if (g_sessionDirty || g_draftsDirty) {
                    Session_Save();
                    g_sessionDirty = FALSE;
                }
                Session_FlushDirtyAsync();
            } else if (wp == 2) {
                KillTimer(hwnd, 2);
                if (g_curDoc >= 0 && g_curDoc < g_docCount && g_docs[g_curDoc].hwndEdit) {
                    int nLines = (int)SendMessage(g_docs[g_curDoc].hwndEdit,
                                                  SCI_GETLINECOUNT, 0, 0);
                    if (nLines <= 5000) Editor_UpdateScrollWidth(g_curDoc);
                }
                ScrollBars_Update();
            } else if (wp == 3) {
                KillTimer(hwnd, 3);
                if (MdView_IsVisible()) MdView_SoftRefresh();
            } else if (wp == 4) {
                KillTimer(hwnd, 4);
                MdEdit_Tick();
            }
            return 0;
        case WM_CLOSE:
            if (ConfirmExit()) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            Session_WaitBackupDone();
            Session_SaveDrafts();
            Session_Save();
            Settings_Save();
            Session_SaveWindow(hwnd);
            KillTimer(hwnd, 1);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static BOOL ForwardFilesToExisting(void) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return FALSE;
    int n = 0;
    for (int i = 1; i < argc; i++)
        if (argv[i] && argv[i][0]) n++;
    if (n == 0) { LocalFree(argv); return FALSE; }
    HWND target = FindWindowW(L"etonClass", NULL);
    if (!target) {
        Sleep(300);
        target = FindWindowW(L"etonClass", NULL);
    }
    if (!target) { LocalFree(argv); return FALSE; }
    size_t bytes = 2 * sizeof(DWORD);
    for (int i = 1; i < argc; i++)
        if (argv[i] && argv[i][0]) bytes += (wcslen(argv[i]) + 1) * sizeof(wchar_t);
    BYTE* buf = (BYTE*)malloc(bytes);
    if (!buf) { LocalFree(argv); return FALSE; }
    ((DWORD*)buf)[0] = ETON_COPYDATA_MAGIC;
    ((DWORD*)buf)[1] = (DWORD)n;
    wchar_t* p = (wchar_t*)(buf + 2 * sizeof(DWORD));
    for (int i = 1; i < argc; i++) {
        if (!argv[i] || !argv[i][0]) continue;
        size_t l = wcslen(argv[i]) + 1;
        memcpy(p, argv[i], l * sizeof(wchar_t));
        p += l;
    }
    COPYDATASTRUCT cds;
    cds.dwData = ETON_COPYDATA_MAGIC;
    cds.cbData = (DWORD)bytes;
    cds.lpData = buf;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt) {
            Sleep(300);
            HWND again = FindWindowW(L"etonClass", NULL);
            if (again) target = again;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        if (pid) AllowSetForegroundWindow(pid);
        DWORD_PTR res = 0;
        SendMessageTimeoutW(target, WM_COPYDATA, 0, (LPARAM)&cds,
                            SMTO_ABORTIFHUNG, 4000, &res);
        if (res) { free(buf); LocalFree(argv); return TRUE; }
    }
    free(buf);
    LocalFree(argv);
    return FALSE;
}
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)cmdLine; (void)hPrev;
    if (ForwardFilesToExisting()) return 0;
    g_hInst = hInst;
    I18n_Init();
    Settings_Load();
    I18n_ApplySystemThemeMode();
    g_dpi = GetDpiForSystem();
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
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        UI_Scale(900), UI_Scale(640), NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    {
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi && dpi != g_dpi) { g_dpi = dpi; Editor_OnDpiChanged(); }
    }
    ShowWindow(hwnd, Session_RestoreWindow(hwnd, nShow));
    UpdateWindow(hwnd);
    HACCEL accel = LoadAcceleratorsW(hInst, MAKEINTRESOURCEW(IDR_ACCEL));
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hFindDlg && IsDialogMessageW(g_hFindDlg, &msg)) continue;
        if (!TranslateAcceleratorW(hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
