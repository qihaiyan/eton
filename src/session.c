#include "common.h"

static BOOL Session_GetDir(wchar_t* out, DWORD cch) {
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return FALSE;
    _snwprintf(out, cch, L"%s\\eton", appdata);
    if (cch) out[cch - 1] = L'\0';
    CreateDirectoryW(out, NULL);
    return TRUE;
}

BOOL Session_IniPath(wchar_t* out, DWORD cch) {
    wchar_t dir[MAX_PATH];
    if (!Session_GetDir(dir, MAX_PATH)) return FALSE;
    _snwprintf(out, cch, L"%s\\session.ini", dir);
    if (cch) out[cch - 1] = L'\0';
    if (!PathFileExistsW(out)) {
        HANDLE h = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            BYTE bom[2] = { 0xFF, 0xFE };
            DWORD wr; WriteFile(h, bom, 2, &wr, NULL);
            CloseHandle(h);
        }
    }
    return TRUE;
}

static BOOL Session_GetDraftDir(wchar_t* out, DWORD cch) {
    wchar_t dir[MAX_PATH];
    if (!Session_GetDir(dir, MAX_PATH)) return FALSE;
    _snwprintf(out, cch, L"%s\\drafts", dir);
    if (cch) out[cch - 1] = L'\0';
    CreateDirectoryW(out, NULL);
    return TRUE;
}

void Session_Save(void) {
    wchar_t ini[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) return;
    int n = 0, activeTab = 0;
    for (int i = 0; i < g_docCount; i++) {
        Doc* d = &g_docs[i];
        const wchar_t* val = NULL;
        if (!d->isNew && d->path[0] != L'\0') {
            val = d->path;
        } else if (d->isNew && d->path[0] == L'\0') {
            if (d->draft[0] || SendMessage(d->hwndEdit, SCI_GETLENGTH, 0, 0) > 0)
                val = L"*draft*";
        }
        if (!val) continue;
        n++;
        wchar_t key[16];
        wsprintf(key, L"file%d", n);
        WritePrivateProfileStringW(L"session", key, val, ini);
        if (i == g_curDoc) activeTab = n;
    }
    wchar_t cnt[16];
    wsprintf(cnt, L"%d", n);
    WritePrivateProfileStringW(L"session", L"count", cnt, ini);
    wchar_t at[16];
    wsprintf(at, L"%d", activeTab);
    WritePrivateProfileStringW(L"session", L"activetab", at, ini);
}

void Session_SaveDrafts(void) {
    wchar_t ini[MAX_PATH], dir[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) return;
    if (!Session_GetDraftDir(dir, MAX_PATH)) return;
    int n = 0;
    for (int i = 0; i < g_docCount; i++) {
        Doc* d = &g_docs[i];
        if (!d->isNew || d->path[0] != L'\0') continue;
        if (SendMessage(d->hwndEdit, SCI_GETLENGTH, 0, 0) == 0) {
            if (d->draft[0]) {
                wchar_t p[MAX_PATH];
                wsprintf(p, L"%s\\%s", dir, d->draft);
                DeleteFileW(p);
                d->draft[0] = L'\0';
            }
            continue;
        }
        if (!d->draft[0]) {
            for (int k = 1; k < 1000; k++) {
                wchar_t nm[32], p[MAX_PATH];
                wsprintf(nm, L"draft%d.txt", k);
                wsprintf(p, L"%s\\%s", dir, nm);
                if (!PathFileExistsW(p)) {
                    wcscpy_s(d->draft, 64, nm);
                    break;
                }
            }
        }
        if (!d->draft[0]) continue;
        DWORD len = 0;
        char* text = Editor_GetTextUtf8(i, &len);
        if (!text) continue;
        wchar_t p[MAX_PATH];
        wsprintf(p, L"%s\\%s", dir, d->draft);
        HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wr;
            WriteFile(h, text, len, &wr, NULL);
            CloseHandle(h);
            n++;
            wchar_t key[16];
            wsprintf(key, L"draft%d", n);
            WritePrivateProfileStringW(L"session", key, d->draft, ini);
        }
        free(text);
    }
    wchar_t v[16];
    wsprintf(v, L"%d", n);
    WritePrivateProfileStringW(L"session", L"drafts", v, ini);
    g_draftsDirty = FALSE;
}

void Session_DiscardDraft(int index) {
    if (index < 0 || index >= g_docCount) return;
    Doc* d = &g_docs[index];
    if (!d->draft[0]) return;
    wchar_t dir[MAX_PATH];
    if (!Session_GetDraftDir(dir, MAX_PATH)) return;
    wchar_t p[MAX_PATH];
    wsprintf(p, L"%s\\%s", dir, d->draft);
    DeleteFileW(p);
    d->draft[0] = L'\0';
}

static int Session_OpenDraft(const wchar_t* draftdir, const wchar_t* name, int seq) {
    wchar_t dp[MAX_PATH], title[32];
    wsprintf(dp, L"%s\\%s", draftdir, name);
    HANDLE h = CreateFileW(dp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD size = GetFileSize(h, NULL);
    char* buf = (char*)malloc((size_t)size + 1);
    DWORD rd = 0;
    if (buf) {
        if (size) ReadFile(h, buf, size, &rd, NULL);
        buf[rd] = '\0';
    }
    CloseHandle(h);
    if (!buf) return -1;
    wsprintf(title, T(STR_UNTITLED_N), seq);
    int ni = Editor_NewDoc(NULL, title, TRUE);
    if (ni >= 0) {
        wcscpy_s(g_docs[ni].draft, 64, name);
        Editor_SetText(ni, buf);
        Editor_MarkDirty(ni, TRUE);
        Editor_Activate(ni);
    }
    free(buf);
    return ni;
}

int Session_Restore(void) {
    wchar_t ini[MAX_PATH], draftdir[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) return 0;
    if (!Session_GetDraftDir(draftdir, MAX_PATH)) return 0;
    int count = (int)GetPrivateProfileIntW(L"session", L"count", 0, ini);
    if (count > MAX_DOCS) count = MAX_DOCS;

    static wchar_t tabs[MAX_DOCS][MAX_PATH];
    BOOL isDraft[MAX_DOCS];
    int nTabs = 0;
    BOOL hasMarker = FALSE;
    for (int i = 1; i <= count; i++) {
        wchar_t key[16], v[MAX_PATH];
        wsprintf(key, L"file%d", i);
        GetPrivateProfileStringW(L"session", key, L"", v, MAX_PATH, ini);
        if (!v[0]) continue;
        if (wcscmp(v, L"*draft*") == 0) { isDraft[nTabs] = TRUE; hasMarker = TRUE; }
        else isDraft[nTabs] = FALSE;
        wcscpy_s(tabs[nTabs], MAX_PATH, v);
        nTabs++;
    }

    int drafts = (int)GetPrivateProfileIntW(L"session", L"drafts", 0, ini);
    if (drafts > MAX_DOCS) drafts = MAX_DOCS;
    static wchar_t dnames[MAX_DOCS][64];
    for (int k = 1; k <= drafts; k++) {
        wchar_t key[16];
        wsprintf(key, L"draft%d", k);
        GetPrivateProfileStringW(L"session", key, L"", dnames[k - 1], 64, ini);
        if (!dnames[k - 1][0]) wsprintf(dnames[k - 1], L"draft%d.txt", k);
    }

    int savedActiveTab = (int)GetPrivateProfileIntW(L"session", L"activetab", 0, ini);
    wchar_t savedActiveFile[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"session", L"activefile", L"", savedActiveFile, MAX_PATH, ini);
    int savedActiveDraft = (int)GetPrivateProfileIntW(L"session", L"activedraft", 0, ini);

    int docIdx[MAX_DOCS * 2];
    int total = 0, opened = 0, nextDraft = 1;
    for (int t = 0; t < nTabs; t++) {
        int di = -1;
        if (isDraft[t]) {
            if (nextDraft <= drafts) {
                di = Session_OpenDraft(draftdir, dnames[nextDraft - 1], nextDraft);
                nextDraft++;
            }
        } else if (PathFileExistsW(tabs[t])) {
            int before = g_docCount;
            OpenFileByPath(tabs[t]);
            di = (g_docCount > before) ? g_docCount - 1 : -1;
        }
        docIdx[total++] = di;
        if (di >= 0) opened++;
    }
    if (!hasMarker) {
        for (int k = nextDraft; k <= drafts; k++) {
            int di = Session_OpenDraft(draftdir, dnames[k - 1], k);
            docIdx[total++] = di;
            if (di >= 0) opened++;
        }
    }

    {
        wchar_t find[MAX_PATH];
        wsprintf(find, L"%s\\*.txt", draftdir);
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(find, &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            do {
                BOOL used = FALSE;
                for (int i = 0; i < g_docCount; i++)
                    if (g_docs[i].isNew && _wcsicmp(g_docs[i].draft, fd.cFileName) == 0) { used = TRUE; break; }
                if (!used) {
                    wchar_t p[MAX_PATH];
                    wsprintf(p, L"%s\\%s", draftdir, fd.cFileName);
                    DeleteFileW(p);
                }
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
    }

    if (opened == 0) return 0;

    if (savedActiveTab >= 1 && savedActiveTab <= total && docIdx[savedActiveTab - 1] >= 0) {
        Editor_Activate(docIdx[savedActiveTab - 1]);
    } else if (savedActiveFile[0]) {
        int ai = Editor_FindDocByPath(savedActiveFile);
        if (ai >= 0) Editor_Activate(ai);
    } else if (!hasMarker) {
        int idx = nTabs + savedActiveDraft - 1;
        if (savedActiveDraft >= 1 && idx >= 0 && idx < total && docIdx[idx] >= 0)
            Editor_Activate(docIdx[idx]);
    }
    return opened;
}

#define WIN_MINVIS 60

static void Session_CenterOnMonitor(HWND hwnd) {
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return;
    SetWindowPos(hwnd, NULL,
                 mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - (rc.right - rc.left)) / 2,
                 mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top)  - (rc.bottom - rc.top)) / 2,
                 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}

void Session_SaveWindow(HWND hwnd) {
    WINDOWPLACEMENT wp; wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp)) return;
    RECT rc = wp.rcNormalPosition;
    wchar_t ini[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) return;
    wchar_t v[16];
    wsprintf(v, L"%d", (int)rc.left);   WritePrivateProfileStringW(L"window", L"left",   v, ini);
    wsprintf(v, L"%d", (int)rc.top);    WritePrivateProfileStringW(L"window", L"top",    v, ini);
    wsprintf(v, L"%d", (int)(rc.right - rc.left));  WritePrivateProfileStringW(L"window", L"width",  v, ini);
    wsprintf(v, L"%d", (int)(rc.bottom - rc.top));  WritePrivateProfileStringW(L"window", L"height", v, ini);
    WritePrivateProfileStringW(L"window", L"max", wp.showCmd == SW_SHOWMAXIMIZED ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"window", L"valid", L"1", ini);
}

int Session_RestoreWindow(HWND hwnd, int nShowCmd) {
    wchar_t ini[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) { Session_CenterOnMonitor(hwnd); return nShowCmd; }
    if (GetPrivateProfileIntW(L"window", L"valid", 0, ini) == 0) {
        Session_CenterOnMonitor(hwnd);
        return nShowCmd;
    }
    LONG l = GetPrivateProfileIntW(L"window", L"left",   0, ini);
    LONG t = GetPrivateProfileIntW(L"window", L"top",    0, ini);
    LONG w = GetPrivateProfileIntW(L"window", L"width",  0, ini);
    LONG h = GetPrivateProfileIntW(L"window", L"height", 0, ini);
    BOOL max = GetPrivateProfileIntW(L"window", L"max",  0, ini) != 0;
    if (w < 200 || h < 120) {
        Session_CenterOnMonitor(hwnd);
        return nShowCmd;
    }
    RECT rc = { l, t, l + w, t + h };
    HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) { Session_CenterOnMonitor(hwnd); return nShowCmd; }
    RECT wa = mi.rcWork;
    if (w > wa.right - wa.left) { w = wa.right - wa.left; rc.right = rc.left + w; }
    if (h > wa.bottom - wa.top) { h = wa.bottom - wa.top; rc.bottom = rc.top + h; }
    if (rc.right < wa.left + WIN_MINVIS) OffsetRect(&rc, wa.left + WIN_MINVIS - rc.right, 0);
    if (rc.bottom < wa.top + WIN_MINVIS) OffsetRect(&rc, 0, wa.top + WIN_MINVIS - rc.bottom);
    if (rc.left > wa.right - WIN_MINVIS) OffsetRect(&rc, wa.right - WIN_MINVIS - rc.left, 0);
    if (rc.top > wa.bottom - WIN_MINVIS) OffsetRect(&rc, 0, wa.bottom - WIN_MINVIS - rc.top);
    SetWindowPos(hwnd, NULL, rc.left, rc.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    return max ? SW_SHOWMAXIMIZED : nShowCmd;
}

void Settings_Load(void) {
    wchar_t ini[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) return;
    g_dark      = GetPrivateProfileIntW(L"settings", L"dark",    1, ini) != 0;
    g_wordWrap  = GetPrivateProfileIntW(L"settings", L"wrap",    0, ini) != 0;
    g_showGutter= GetPrivateProfileIntW(L"settings", L"gutter",  1, ini) != 0;
    int fs = (int)GetPrivateProfileIntW(L"settings", L"fontsize", 11, ini);
    if (fs >= 6 && fs <= 48) g_fontSize = fs;
}

void Settings_Save(void) {
    wchar_t ini[MAX_PATH];
    if (!Session_IniPath(ini, MAX_PATH)) return;
    WritePrivateProfileStringW(L"settings", L"dark",     g_dark ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"settings", L"wrap",     g_wordWrap ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"settings", L"gutter",   g_showGutter ? L"1" : L"0", ini);
    wchar_t v[16]; wsprintf(v, L"%d", g_fontSize);
    WritePrivateProfileStringW(L"settings", L"fontsize", v, ini);
}

#define AUTOBACK_MAX (4 * 1024 * 1024)

static BOOL Session_AutoBackupPath(const wchar_t* path, wchar_t* out, DWORD cch) {
    if (!path || !path[0]) return FALSE;
    wchar_t dir[MAX_PATH];
    if (!Session_GetDir(dir, MAX_PATH)) return FALSE;
    wchar_t sub[MAX_PATH];
    _snwprintf(sub, MAX_PATH, L"%s\\autoback", dir);
    sub[MAX_PATH - 1] = L'\0';
    CreateDirectoryW(sub, NULL);
    unsigned long long h = 1469598103934665603ULL;
    for (const wchar_t* p = path; *p; p++) { h ^= (unsigned)*p; h *= 1099511628211ULL; }
    _snwprintf(out, cch, L"%s\\%016I64x.txt", sub, h);
    if (cch) out[cch - 1] = L'\0';
    return TRUE;
}

void Session_AutoBackupWrite(int index) {
    if (index < 0 || index >= g_docCount) return;
    Doc* d = &g_docs[index];
    if (d->isNew || !d->dirty || d->path[0] == L'\0') return;
    if (!d->hwndEdit ||
        SendMessage(d->hwndEdit, SCI_GETLENGTH, 0, 0) > (LRESULT)AUTOBACK_MAX) return;
    wchar_t p[MAX_PATH];
    if (!Session_AutoBackupPath(d->path, p, MAX_PATH)) return;
    DWORD len = 0;
    char* text = Editor_GetTextUtf8(index, &len);
    if (!text) return;
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr; WriteFile(h, text, len, &wr, NULL);
        CloseHandle(h);
    }
    free(text);
}

static BOOL Session_AutoBackupPathOf(const wchar_t* path, wchar_t* out, DWORD cch) {
    return Session_AutoBackupPath(path, out, cch);
}

void Session_AutoBackupDiscard(const wchar_t* path) {
    wchar_t p[MAX_PATH];
    if (Session_AutoBackupPathOf(path, p, MAX_PATH)) DeleteFileW(p);
}

BOOL Session_AutoBackupExists(const wchar_t* path) {
    wchar_t p[MAX_PATH];
    return Session_AutoBackupPathOf(path, p, MAX_PATH) ? PathFileExistsW(p) : FALSE;
}

BOOL Session_AutoBackupRead(const wchar_t* path, char** out, DWORD* len) {
    *out = NULL; *len = 0;
    wchar_t p[MAX_PATH];
    if (!Session_AutoBackupPathOf(path, p, MAX_PATH)) return FALSE;
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD size = GetFileSize(h, NULL);
    char* buf = (char*)malloc((size_t)size + 1);
    DWORD rd = 0;
    if (buf) {
        if (size) ReadFile(h, buf, size, &rd, NULL);
        buf[rd] = '\0';
    }
    CloseHandle(h);
    if (!buf) return FALSE;
    *out = buf; *len = rd;
    return TRUE;
}
