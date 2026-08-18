#include "common.h"
#include <wchar.h>

wchar_t g_findText[512];
wchar_t g_replText[512];
BOOL g_findCase = FALSE, g_findWord = FALSE, g_findDown = TRUE;

LONG g_findStart = 0;

/* Scintilla uses UTF-8 internally; our search UI uses wchar_t.
   DoFindFrom works on wchar_t by pulling UTF-8 text and converting.
   Position returned is a Scintilla byte position. */
LONG DoFindFrom(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL down, LONG start, BOOL wrap, BOOL* found) {
    *found = FALSE;
    if (!hed) return -1;
    /* Convert search text to UTF-8 */
    char utf8[1024];
    int u8len = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    if (u8len <= 0) return -1;
    u8len--; /* exclude null terminator */

    Sci_Position docLen = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    if (docLen == 0) return -1;

    int flags = 0;
    if (cs) flags |= SCFIND_MATCHCASE;
    if (ww) flags |= SCFIND_WHOLEWORD;
    SendMessage(hed, SCI_SETSEARCHFLAGS, flags, 0);

    /* First search from given start */
    if (down) {
        SendMessage(hed, SCI_SETTARGETSTART, start, 0);
        SendMessage(hed, SCI_SETTARGETEND, docLen, 0);
    } else {
        SendMessage(hed, SCI_SETTARGETSTART, start, 0);
        SendMessage(hed, SCI_SETTARGETEND, 0, 0);
    }
    Sci_Position pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8);
    if (pos >= 0) {
        SendMessage(hed, SCI_SETSEL, pos, pos + u8len);
        SendMessage(hed, SCI_SCROLLCARET, 0, 0);
        *found = TRUE;
        return (LONG)pos;
    }
    /* Wrap search */
    if (wrap) {
        if (down) {
            SendMessage(hed, SCI_SETTARGETSTART, 0, 0);
            SendMessage(hed, SCI_SETTARGETEND, start, 0);
        } else {
            SendMessage(hed, SCI_SETTARGETSTART, docLen, 0);
            SendMessage(hed, SCI_SETTARGETEND, start, 0);
        }
        pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8);
        if (pos >= 0) {
            SendMessage(hed, SCI_SETSEL, pos, pos + u8len);
            SendMessage(hed, SCI_SCROLLCARET, 0, 0);
            *found = TRUE;
            return (LONG)pos;
        }
    }
    return -1;
}

/* ---------------- Find ---------------- */
static INT_PTR CALLBACK FindProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hed = NULL;
    if (msg == WM_INITDIALOG) {
        hed = Editor_ActiveEdit();
        SetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText);
        CheckDlgButton(hdlg, IDC_FIND_CASE, g_findCase ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_WORD, g_findWord ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_DOWN, g_findDown ? BST_CHECKED : BST_UNCHECKED);
        if (hed) {
            g_findStart = (LONG)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_FIND_CLOSE || id == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
        if (id == IDC_FIND_NEXT || id == IDC_FIND_PREV) {
            GetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText, 512);
            g_findCase = IsDlgButtonChecked(hdlg, IDC_FIND_CASE) == BST_CHECKED;
            g_findWord = IsDlgButtonChecked(hdlg, IDC_FIND_WORD) == BST_CHECKED;
            g_findDown = (id == IDC_FIND_NEXT);
            if (wcslen(g_findText) == 0) { SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"请输入查找内容"); return TRUE; }
            BOOL found;
            LONG pos = DoFindFrom(hed, g_findText, g_findCase, g_findWord, g_findDown, g_findStart, TRUE, &found);
            if (found) {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"已找到匹配项");
                char utf8[1024];
                int u8len = WideCharToMultiByte(CP_UTF8, 0, g_findText, -1, utf8, sizeof(utf8), NULL, NULL) - 1;
                g_findStart = g_findDown ? (pos + u8len) : pos;
            } else {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"未找到匹配项");
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------------- Replace ---------------- */
static INT_PTR CALLBACK ReplaceProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hed = NULL;
    if (msg == WM_INITDIALOG) {
        hed = Editor_ActiveEdit();
        SetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText);
        SetDlgItemTextW(hdlg, IDC_REPL_TEXT, g_replText);
        CheckDlgButton(hdlg, IDC_FIND_CASE, g_findCase ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_WORD, g_findWord ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_DOWN, g_findDown ? BST_CHECKED : BST_UNCHECKED);
        if (hed) { g_findStart = (LONG)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0); }
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_FIND_CLOSE || id == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
        if (id == IDC_FIND_NEXT || id == IDC_FIND_PREV) {
            GetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText, 512);
            g_findCase = IsDlgButtonChecked(hdlg, IDC_FIND_CASE) == BST_CHECKED;
            g_findWord = IsDlgButtonChecked(hdlg, IDC_FIND_WORD) == BST_CHECKED;
            g_findDown = (id == IDC_FIND_NEXT);
            if (wcslen(g_findText) == 0) { SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"请输入查找内容"); return TRUE; }
            BOOL found;
            LONG pos = DoFindFrom(hed, g_findText, g_findCase, g_findWord, g_findDown, g_findStart, TRUE, &found);
            if (found) {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"已找到匹配项");
                char utf8[1024];
                int u8len = WideCharToMultiByte(CP_UTF8, 0, g_findText, -1, utf8, sizeof(utf8), NULL, NULL) - 1;
                g_findStart = g_findDown ? (pos + u8len) : pos;
            }
            else SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"未找到匹配项");
            return TRUE;
        }
        if (id == IDC_FIND_REPL) {
            GetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText, 512);
            GetDlgItemTextW(hdlg, IDC_REPL_TEXT, g_replText, 512);
            if (wcslen(g_findText) == 0) return TRUE;
            /* Check if current selection matches find text */
            Sci_Position selStart = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONSTART, 0, 0);
            Sci_Position selEnd = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONEND, 0, 0);
            char utf8find[1024];
            int u8len = WideCharToMultiByte(CP_UTF8, 0, g_findText, -1, utf8find, sizeof(utf8find), NULL, NULL) - 1;
            if (selEnd - selStart == u8len) {
                /* Get selected text and compare */
                char selbuf[1024];
                SendMessage(hed, SCI_GETSELTEXT, 0, (LPARAM)selbuf);
                BOOL eq = g_findCase ? (strcmp(selbuf, utf8find) == 0) : (_stricmp(selbuf, utf8find) == 0);
                if (eq) {
                    char utf8repl[1024];
                    WideCharToMultiByte(CP_UTF8, 0, g_replText, -1, utf8repl, sizeof(utf8repl), NULL, NULL);
                    SendMessage(hed, SCI_REPLACESEL, 0, (LPARAM)utf8repl);
                    LONG pos = (LONG)(selStart + strlen(utf8repl));
                    g_findStart = pos;
                    BOOL found; DoFindFrom(hed, g_findText, g_findCase, g_findWord, g_findDown, g_findStart, TRUE, &found);
                    SetDlgItemTextW(hdlg, IDC_FIND_STATUS, L"已替换");
                }
            }
            return TRUE;
        }
        if (id == IDC_FIND_REPLALL) {
            GetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText, 512);
            GetDlgItemTextW(hdlg, IDC_REPL_TEXT, g_replText, 512);
            if (wcslen(g_findText) == 0) return TRUE;
            char utf8find[1024], utf8repl[1024];
            int u8len = WideCharToMultiByte(CP_UTF8, 0, g_findText, -1, utf8find, sizeof(utf8find), NULL, NULL) - 1;
            WideCharToMultiByte(CP_UTF8, 0, g_replText, -1, utf8repl, sizeof(utf8repl), NULL, NULL);
            int count = 0;
            /* Target whole doc */
            SendMessage(hed, SCI_SETTARGETSTART, 0, 0);
            SendMessage(hed, SCI_SETTARGETEND, (WPARAM)SendMessage(hed, SCI_GETLENGTH, 0, 0), 0);
            int flags = 0;
            if (g_findCase) flags |= SCFIND_MATCHCASE;
            if (g_findWord) flags |= SCFIND_WHOLEWORD;
            SendMessage(hed, SCI_SETSEARCHFLAGS, flags, 0);
            Sci_Position pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8find);
            while (pos >= 0) {
                /* Replace: set target to found range and replace */
                Sci_Position tend = (Sci_Position)SendMessage(hed, SCI_GETTARGETEND, 0, 0);
                SendMessage(hed, SCI_SETTARGETSTART, pos, 0);
                SendMessage(hed, SCI_SETTARGETEND, tend, 0);
                SendMessage(hed, SCI_REPLACETARGET, -1, (LPARAM)utf8repl);
                count++;
                /* Continue from after replacement */
                Sci_Position newpos = pos + (Sci_Position)strlen(utf8repl);
                SendMessage(hed, SCI_SETTARGETSTART, newpos, 0);
                SendMessage(hed, SCI_SETTARGETEND, (WPARAM)SendMessage(hed, SCI_GETLENGTH, 0, 0), 0);
                pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8find);
            }
            wchar_t st[64]; wsprintf(st, L"已替换 %d 处", count);
            SetDlgItemTextW(hdlg, IDC_FIND_STATUS, st);
            Editor_MarkDirty(g_curDoc, TRUE);
            return TRUE;
        }
    }
    return FALSE;
}

void Dlg_Find(HWND hwnd, BOOL replace) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(replace ? IDD_REPLACE : IDD_FIND), hwnd, replace ? ReplaceProc : FindProc, 0);
}

/* ---------------- Goto ---------------- */
static INT_PTR CALLBACK GotoProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hed = NULL;
    if (msg == WM_INITDIALOG) {
        hed = Editor_ActiveEdit();
        int total = hed ? (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0) : 0;
        wchar_t lbl[64]; wsprintf(lbl, L"行号 (1 - %d):", total);
        SetDlgItemTextW(hdlg, IDC_GOTO_LABEL, lbl);
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_GOTO_CANCEL || id == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
        if (id == IDC_GOTO_OK) {
            wchar_t buf[32]; GetDlgItemTextW(hdlg, IDC_GOTO_TEXT, buf, 32);
            int line = _wtoi(buf);
            int total = hed ? (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0) : 0;
            if (line < 1) line = 1;
            if (line > total) line = total;
            if (hed && total > 0) {
                /* SCI_GOTOLINE scrolls the line into view and places caret */
                SendMessage(hed, SCI_GOTOLINE, line - 1, 0);
            }
            EndDialog(hdlg, 1);
            return TRUE;
        }
    }
    return FALSE;
}

void Dlg_Goto(HWND hwnd) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_GOTO), hwnd, GotoProc, 0);
}

/* ---------------- About ---------------- */
static INT_PTR CALLBACK AboutProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) return TRUE;
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDC_GOTO_OK || LOWORD(wp) == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
    }
    return FALSE;
}
void Dlg_About(HWND hwnd) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_ABOUT), hwnd, AboutProc, 0);
}

/* ---------------- Open with encoding ---------------- */
static const struct { Encoding enc; const wchar_t* name; } g_encList[] = {
    { ENC_ANSI,     L"ANSI" },
    { ENC_UTF8,     L"UTF-8" },
    { ENC_UTF8_BOM, L"UTF-8 (带 BOM)" },
    { ENC_UTF16LE,  L"UTF-16 LE" },
    { ENC_UTF16BE,  L"UTF-16 BE" },
};

static INT_PTR CALLBACK OpenEncProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hlist = NULL;
    if (msg == WM_INITDIALOG) {
        hlist = GetDlgItem(hdlg, IDC_OPENENC_LIST);
        for (int i = 0; i < 5; i++) SendMessageW(hlist, LB_ADDSTRING, 0, (LPARAM)g_encList[i].name);
        SendMessageW(hlist, LB_SETCURSEL, 0, 0);
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_GOTO_CANCEL || id == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
        if (id == IDC_GOTO_OK) {
            int sel = (int)SendMessageW(hlist, LB_GETCURSEL, 0, 0);
            if (sel < 0) sel = 0;
            EndDialog(hdlg, (INT_PTR)(g_encList[sel].enc + 1));
            return TRUE;
        }
    }
    return FALSE;
}

void Dlg_OpenEnc(HWND hwnd) {
    INT_PTR r = DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_OPENENC), hwnd, OpenEncProc, 0);
    if (r <= 0) return;
    Encoding enc = (Encoding)(r - 1);
    wchar_t fname[MAX_PATH] = {0};
    wchar_t filter[] = L"所有文件 (*.*)\0*.*\0文本文件 (*.txt)\0*.txt\0";
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fname;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    int existing = Editor_FindDocByPath(fname);
    if (existing >= 0) { Editor_Activate(existing); return; }
    int ni = Editor_NewDoc(fname, NULL, FALSE);
    if (ni < 0) return;
    if (!Editor_LoadFile(ni, fname, enc)) {
        Editor_CloseDoc(ni);
        return;
    }
    AddRecent(fname);
    Editor_Activate(ni);
}
