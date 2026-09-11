#include "common.h"
#include <wchar.h>

wchar_t g_findText[512];
wchar_t g_replText[512];
BOOL g_findCase = FALSE, g_findWord = FALSE, g_findDown = TRUE;
BOOL g_findRegex = FALSE;

HWND g_hFindDlg = NULL;

LONG g_findStart = 0;

LONG DoFindFrom(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL re, BOOL down, LONG start, BOOL wrap, BOOL* found) {
    *found = FALSE;
    if (!hed) return -1;
    char utf8[1024];
    int u8len = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    if (u8len <= 0) return -1;
    u8len--;

    Sci_Position docLen = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    if (docLen == 0) return -1;

    int flags = 0;
    if (cs) flags |= SCFIND_MATCHCASE;
    if (ww) flags |= SCFIND_WHOLEWORD;
    if (re) flags |= SCFIND_REGEXP;
    SendMessage(hed, SCI_SETSEARCHFLAGS, flags, 0);

    SendMessage(hed, SCI_SETTARGETSTART, start, 0);
    SendMessage(hed, SCI_SETTARGETEND, down ? docLen : 0, 0);
    Sci_Position pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8);
    if (pos >= 0) {
        SendMessage(hed, SCI_SETSEL, pos, (WPARAM)SendMessage(hed, SCI_GETTARGETEND, 0, 0));
        SendMessage(hed, SCI_SCROLLCARET, 0, 0);
        *found = TRUE;
        return down ? (LONG)SendMessage(hed, SCI_GETTARGETEND, 0, 0) : (LONG)pos;
    }
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
            SendMessage(hed, SCI_SETSEL, pos, (WPARAM)SendMessage(hed, SCI_GETTARGETEND, 0, 0));
            SendMessage(hed, SCI_SCROLLCARET, 0, 0);
            *found = TRUE;
            return down ? (LONG)SendMessage(hed, SCI_GETTARGETEND, 0, 0) : (LONG)pos;
        }
    }
    return -1;
}

void Find_MarkAll(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL re) {
    if (!hed) return;
    SendMessage(hed, SCI_SETINDICATORCURRENT, 20, 0);
    Sci_Position docLen = (Sci_Position)SendMessage(hed, SCI_GETLENGTH, 0, 0);
    SendMessage(hed, SCI_INDICATORCLEARRANGE, 0, docLen);
    if (!text || !text[0] || docLen == 0) return;
    char utf8[1024];
    int u8len = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    if (u8len <= 0) return;
    u8len--;
    int flags = 0;
    if (cs) flags |= SCFIND_MATCHCASE;
    if (ww) flags |= SCFIND_WHOLEWORD;
    if (re) flags |= SCFIND_REGEXP;
    SendMessage(hed, SCI_SETSEARCHFLAGS, flags, 0);
    SendMessage(hed, SCI_SETTARGETSTART, 0, 0);
    SendMessage(hed, SCI_SETTARGETEND, docLen, 0);
    int count = 0;
    Sci_Position pos;
    while (count < 2000 &&
           (pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8)) >= 0) {
        Sci_Position end = (Sci_Position)SendMessage(hed, SCI_GETTARGETEND, 0, 0);
        if (end <= pos) end = pos + 1;
        SendMessage(hed, SCI_INDICATORFILLRANGE, pos, end - pos);
        SendMessage(hed, SCI_SETTARGETSTART, end, 0);
        SendMessage(hed, SCI_SETTARGETEND, docLen, 0);
        count++;
    }
}

static void ReadFindOptions(HWND hdlg, HWND* lastEdit) {
    GetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText, 512);
    g_findCase = IsDlgButtonChecked(hdlg, IDC_FIND_CASE) == BST_CHECKED;
    g_findWord = IsDlgButtonChecked(hdlg, IDC_FIND_WORD) == BST_CHECKED;
    g_findRegex = IsDlgButtonChecked(hdlg, IDC_FIND_REGEX) == BST_CHECKED;
    HWND hed = Editor_ActiveEdit();
    if (hed != *lastEdit) {
        *lastEdit = hed;
        g_findStart = (hed) ? (LONG)SendMessage(hed, SCI_GETCURRENTPOS, 0, 0) : 0;
    }
}

static void DestroyFindDlg(HWND hdlg) {
    g_hFindDlg = NULL;
    DestroyWindow(hdlg);
}

static INT_PTR CALLBACK FindProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND s_lastEdit = NULL;
    if (msg == WM_INITDIALOG) {
        I18n_ApplyDialog(hdlg, IDD_FIND);
        s_lastEdit = Editor_ActiveEdit();
        g_findStart = (s_lastEdit) ? (LONG)SendMessage(s_lastEdit, SCI_GETCURRENTPOS, 0, 0) : 0;
        SetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText);
        CheckDlgButton(hdlg, IDC_FIND_CASE, g_findCase ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_WORD, g_findWord ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_DOWN, g_findDown ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_REGEX, g_findRegex ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (msg == WM_CLOSE) { DestroyFindDlg(hdlg); return TRUE; }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_FIND_CLOSE || id == IDCANCEL) { DestroyFindDlg(hdlg); return TRUE; }
        if (id == IDC_FIND_NEXT || id == IDC_FIND_PREV) {
            ReadFindOptions(hdlg, &s_lastEdit);
            g_findDown = (id == IDC_FIND_NEXT);
            HWND hed = s_lastEdit;
            if (wcslen(g_findText) == 0) { SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_ENTER_TEXT)); return TRUE; }
            BOOL found;
            LONG pos = DoFindFrom(hed, g_findText, g_findCase, g_findWord, g_findRegex,
                                  g_findDown, g_findStart, TRUE, &found);
            if (found) {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_MATCH_FOUND));
                g_findStart = pos;
                Find_MarkAll(hed, g_findText, g_findCase, g_findWord, g_findRegex);
            } else {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_NO_MATCH));
                Find_MarkAll(hed, L"", FALSE, FALSE, FALSE);
            }
            return TRUE;
        }
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}

static INT_PTR CALLBACK ReplaceProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND s_lastEdit = NULL;
    if (msg == WM_INITDIALOG) {
        I18n_ApplyDialog(hdlg, IDD_REPLACE);
        s_lastEdit = Editor_ActiveEdit();
        g_findStart = (s_lastEdit) ? (LONG)SendMessage(s_lastEdit, SCI_GETCURRENTPOS, 0, 0) : 0;
        SetDlgItemTextW(hdlg, IDC_FIND_TEXT, g_findText);
        SetDlgItemTextW(hdlg, IDC_REPL_TEXT, g_replText);
        CheckDlgButton(hdlg, IDC_FIND_CASE, g_findCase ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_WORD, g_findWord ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_DOWN, g_findDown ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hdlg, IDC_FIND_REGEX, g_findRegex ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (msg == WM_CLOSE) { DestroyFindDlg(hdlg); return TRUE; }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_FIND_CLOSE || id == IDCANCEL) { DestroyFindDlg(hdlg); return TRUE; }
        if (id == IDC_FIND_NEXT || id == IDC_FIND_PREV) {
            ReadFindOptions(hdlg, &s_lastEdit);
            g_findDown = (id == IDC_FIND_NEXT);
            HWND hed = s_lastEdit;
            if (wcslen(g_findText) == 0) { SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_ENTER_TEXT)); return TRUE; }
            BOOL found;
            LONG pos = DoFindFrom(hed, g_findText, g_findCase, g_findWord, g_findRegex,
                                  g_findDown, g_findStart, TRUE, &found);
            if (found) {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_MATCH_FOUND));
                g_findStart = pos;
                Find_MarkAll(hed, g_findText, g_findCase, g_findWord, g_findRegex);
            }
            else {
                SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_NO_MATCH));
                Find_MarkAll(hed, L"", FALSE, FALSE, FALSE);
            }
            return TRUE;
        }
        if (id == IDC_FIND_REPL) {
            ReadFindOptions(hdlg, &s_lastEdit);
            HWND hed = s_lastEdit;
            if (!hed || wcslen(g_findText) == 0) return TRUE;
            char utf8find[1024], utf8repl[1024];
            int u8len = WideCharToMultiByte(CP_UTF8, 0, g_findText, -1, utf8find, sizeof(utf8find), NULL, NULL) - 1;
            WideCharToMultiByte(CP_UTF8, 0, g_replText, -1, utf8repl, sizeof(utf8repl), NULL, NULL);
            if (g_findRegex) {
                SendMessage(hed, SCI_SETSEARCHFLAGS, SCFIND_REGEXP |
                            (g_findCase ? SCFIND_MATCHCASE : 0), 0);
                SendMessage(hed, SCI_SETTARGETSTART, g_findStart, 0);
                SendMessage(hed, SCI_SETTARGETEND,
                            (WPARAM)SendMessage(hed, SCI_GETLENGTH, 0, 0), 0);
                Sci_Position pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8find);
                if (pos >= 0) {
                    SendMessage(hed, SCI_REPLACETARGET, -1, (LPARAM)utf8repl);
                    g_findStart = (LONG)SendMessage(hed, SCI_GETTARGETEND, 0, 0);
                    SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_REPLACED));
                    Editor_MarkDirty(g_curDoc, TRUE);
                    BOOL found;
                    DoFindFrom(hed, g_findText, g_findCase, g_findWord, TRUE,
                               TRUE, g_findStart, TRUE, &found);
                } else {
                    SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_NO_MATCH));
                }
                return TRUE;
            }
            Sci_Position selStart = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONSTART, 0, 0);
            Sci_Position selEnd = (Sci_Position)SendMessage(hed, SCI_GETSELECTIONEND, 0, 0);
            if (selEnd - selStart == u8len) {
                char selbuf[1024];
                SendMessage(hed, SCI_GETSELTEXT, 0, (LPARAM)selbuf);
                BOOL eq = g_findCase ? (strcmp(selbuf, utf8find) == 0) : (_stricmp(selbuf, utf8find) == 0);
                if (eq) {
                    SendMessage(hed, SCI_REPLACESEL, 0, (LPARAM)utf8repl);
                    LONG pos = (LONG)(selStart + strlen(utf8repl));
                    g_findStart = pos;
                    BOOL found; DoFindFrom(hed, g_findText, g_findCase, g_findWord, FALSE,
                                           g_findDown, g_findStart, TRUE, &found);
                    SetDlgItemTextW(hdlg, IDC_FIND_STATUS, T(STR_REPLACED));
                }
            }
            return TRUE;
        }
        if (id == IDC_FIND_REPLALL) {
            ReadFindOptions(hdlg, &s_lastEdit);
            HWND hed = s_lastEdit;
            if (!hed || wcslen(g_findText) == 0) return TRUE;
            char utf8find[1024], utf8repl[1024];
            int u8len = WideCharToMultiByte(CP_UTF8, 0, g_findText, -1, utf8find, sizeof(utf8find), NULL, NULL) - 1;
            int rlen = WideCharToMultiByte(CP_UTF8, 0, g_replText, -1, utf8repl, sizeof(utf8repl), NULL, NULL) - 1;
            if (rlen < 0) rlen = 0;
            int count = 0;
            SendMessage(hed, SCI_SETTARGETSTART, 0, 0);
            SendMessage(hed, SCI_SETTARGETEND, (WPARAM)SendMessage(hed, SCI_GETLENGTH, 0, 0), 0);
            int flags = 0;
            if (g_findCase) flags |= SCFIND_MATCHCASE;
            if (g_findWord) flags |= SCFIND_WHOLEWORD;
            if (g_findRegex) flags |= SCFIND_REGEXP;
            SendMessage(hed, SCI_SETSEARCHFLAGS, flags, 0);
            Sci_Position pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8find);
            while (pos >= 0) {
                Sci_Position tend = (Sci_Position)SendMessage(hed, SCI_GETTARGETEND, 0, 0);
                SendMessage(hed, SCI_SETTARGETSTART, pos, 0);
                SendMessage(hed, SCI_SETTARGETEND, tend, 0);
                SendMessage(hed, SCI_REPLACETARGET, -1, (LPARAM)utf8repl);
                count++;
                Sci_Position newpos = pos + rlen;
                if (newpos < tend) newpos = tend;
                if (newpos <= pos) newpos = pos + 1;
                SendMessage(hed, SCI_SETTARGETSTART, newpos, 0);
                SendMessage(hed, SCI_SETTARGETEND, (WPARAM)SendMessage(hed, SCI_GETLENGTH, 0, 0), 0);
                pos = (Sci_Position)SendMessage(hed, SCI_SEARCHINTARGET, u8len, (LPARAM)utf8find);
            }
            wchar_t st[64]; wsprintf(st, T(STR_REPLACED_N), count);
            SetDlgItemTextW(hdlg, IDC_FIND_STATUS, st);
            Find_MarkAll(hed, L"", FALSE, FALSE, FALSE);
            Editor_MarkDirty(g_curDoc, TRUE);
            return TRUE;
        }
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}

void Dlg_Find(HWND hwnd, BOOL replace) {
    if (g_hFindDlg) { DestroyFindDlg(g_hFindDlg); }
    g_hFindDlg = CreateDialogParamW(g_hInst,
                    MAKEINTRESOURCEW(replace ? IDD_REPLACE : IDD_FIND),
                    hwnd, replace ? ReplaceProc : FindProc, 0);
    if (g_hFindDlg) ShowWindow(g_hFindDlg, SW_SHOW);
}

static INT_PTR CALLBACK GotoProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hed = NULL;
    if (msg == WM_INITDIALOG) {
        I18n_ApplyDialog(hdlg, IDD_GOTO);
        hed = Editor_ActiveEdit();
        int total = hed ? (int)SendMessage(hed, SCI_GETLINECOUNT, 0, 0) : 0;
        wchar_t lbl[64]; wsprintf(lbl, T(STR_GOTO_LBL_FMT), total);
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
                SendMessage(hed, SCI_GOTOLINE, line - 1, 0);
            }
            EndDialog(hdlg, 1);
            return TRUE;
        }
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}

void Dlg_Goto(HWND hwnd) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_GOTO), hwnd, GotoProc, 0);
}

static INT_PTR CALLBACK AboutProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) { I18n_ApplyDialog(hdlg, IDD_ABOUT); return TRUE; }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDC_GOTO_OK || LOWORD(wp) == IDCANCEL) { EndDialog(hdlg, 0); return TRUE; }
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}
void Dlg_About(HWND hwnd) {
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_ABOUT), hwnd, AboutProc, 0);
}

static const struct { Encoding enc; } g_encList[] = {
    { ENC_ANSI     },
    { ENC_UTF8     },
    { ENC_UTF8_BOM },
    { ENC_UTF16LE  },
    { ENC_UTF16BE  },
};

static INT_PTR CALLBACK OpenEncProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hlist = NULL;
    if (msg == WM_INITDIALOG) {
        I18n_ApplyDialog(hdlg, IDD_OPENENC);
        hlist = GetDlgItem(hdlg, IDC_OPENENC_LIST);
        for (int i = 0; i < 5; i++) SendMessageW(hlist, LB_ADDSTRING, 0, (LPARAM)EncodingName(g_encList[i].enc));
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
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT ||
        msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX)
        return (INT_PTR)I18n_DlgCtlColor(hdlg, (HDC)wp, msg);
    return FALSE;
}

void Dlg_OpenEnc(HWND hwnd) {
    INT_PTR r = DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_OPENENC), hwnd, OpenEncProc, 0);
    if (r <= 0) return;
    Encoding enc = (Encoding)(r - 1);
    wchar_t fname[MAX_PATH] = {0};
    wchar_t filter[256];
    I18n_JoinFilter(filter, 256, STR_FILTER_ALL, STR_FILTER_ALLPAT,
                             STR_FILTER_TXT, STR_FILTER_TXTPAT);
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
