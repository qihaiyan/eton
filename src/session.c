#include "common.h"

/* ---------------- session：会话与草稿 ----------------
   会话 = 上次的完整标签序列（文件路径 / "*draft*" 草稿占位）+ 激活位置。
   存于 %APPDATA%\eton\session.ini（UTF-16，BOM 起 Unicode 读写作用）。
   草稿 = 未命名文档的内容，存于 %APPDATA%\eton\drafts\ 下各自稳定的文件：
   定时（10s）/ 关闭标签 / 退出时落盘；关闭标签即弃稿；清空或"另存为"后移除。 */

static BOOL Session_GetDir(wchar_t* out, DWORD cch) {
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return FALSE;
    _snwprintf(out, cch, L"%s\\eton", appdata);
    if (cch) out[cch - 1] = L'\0';
    CreateDirectoryW(out, NULL);   /* 已存在时失败，忽略 */
    return TRUE;
}

static BOOL Session_GetIniPath(wchar_t* out, DWORD cch) {
    wchar_t dir[MAX_PATH];
    if (!Session_GetDir(dir, MAX_PATH)) return FALSE;
    _snwprintf(out, cch, L"%s\\session.ini", dir);
    if (cch) out[cch - 1] = L'\0';
    /* 首次创建时写入 UTF-16 BOM，让 ini API 按 Unicode 读写，路径含任何字符都不丢 */
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

/* 保存当前会话：按完整标签顺序记录（fileN = 文件路径或 "*draft*" 占位），
   activetab 为激活标签的位置序号。空的未命名文档不占位。
   在每次标签集/激活变化时调用，异常退出（崩溃/被杀）也不丢会话。 */
void Session_Save(void) {
    wchar_t ini[MAX_PATH];
    if (!Session_GetIniPath(ini, MAX_PATH)) return;
    int n = 0, activeTab = 0;
    for (int i = 0; i < g_docCount; i++) {
        Doc* d = &g_docs[i];
        const wchar_t* val = NULL;
        if (!d->isNew && d->path[0] != L'\0') {
            val = d->path;
        } else if (d->isNew && d->path[0] == L'\0') {
            /* 未命名文档：有内容（或已分配草稿文件）记为草稿占位，保持标签顺序 */
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

/* 保存所有未命名文档（草稿）的内容到 drafts\ 下各自稳定的文件（UTF-8）。
   草稿文件与标签同生命周期保留：关闭标签即弃稿（见 Session_DiscardDraft），
   清空内容或"另存为"后才移除。空文档跳过。 */
void Session_SaveDrafts(void) {
    wchar_t ini[MAX_PATH], dir[MAX_PATH];
    if (!Session_GetIniPath(ini, MAX_PATH)) return;
    if (!Session_GetDraftDir(dir, MAX_PATH)) return;
    int n = 0;
    for (int i = 0; i < g_docCount; i++) {
        Doc* d = &g_docs[i];
        if (!d->isNew || d->path[0] != L'\0') continue;
        if (SendMessage(d->hwndEdit, SCI_GETLENGTH, 0, 0) == 0) {
            if (d->draft[0]) {   /* 清空内容即弃稿 */
                wchar_t p[MAX_PATH];
                wsprintf(p, L"%s\\%s", dir, d->draft);
                DeleteFileW(p);
                d->draft[0] = L'\0';
            }
            continue;
        }
        if (!d->draft[0]) {   /* 首次落盘时分配一个未被占用的草稿文件名 */
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

/* 删除指定文档的草稿文件（关闭草稿标签即弃稿；另存为后移除草稿） */
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

/* 打开一个草稿为未命名文档，返回文档索引（失败 -1） */
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
    wsprintf(title, L"未命名 %d", seq);
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

/* 恢复上次会话：按保存的完整标签顺序还原（文件按路径、草稿按 *draft* 占位），
   返回成功恢复的文档数 */
int Session_Restore(void) {
    wchar_t ini[MAX_PATH], draftdir[MAX_PATH];
    if (!Session_GetIniPath(ini, MAX_PATH)) return 0;
    if (!Session_GetDraftDir(draftdir, MAX_PATH)) return 0;
    int count = (int)GetPrivateProfileIntW(L"session", L"count", 0, ini);
    if (count > MAX_DOCS) count = MAX_DOCS;

    /* 先读入完整标签列表（路径或 *draft* 占位），保证顺序一致 */
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

    /* 草稿文件名列表（draft1..draftN，按保存时的文档顺序编号） */
    int drafts = (int)GetPrivateProfileIntW(L"session", L"drafts", 0, ini);
    if (drafts > MAX_DOCS) drafts = MAX_DOCS;
    static wchar_t dnames[MAX_DOCS][64];
    for (int k = 1; k <= drafts; k++) {
        wchar_t key[16];
        wsprintf(key, L"draft%d", k);
        GetPrivateProfileStringW(L"session", key, L"", dnames[k - 1], 64, ini);
        if (!dnames[k - 1][0]) wsprintf(dnames[k - 1], L"draft%d.txt", k);   /* 兼容旧版按位置命名 */
    }

    /* 激活信息必须在打开任何标签之前读入：打开过程会触发 Session_Save 重写 ini */
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
    /* 旧版格式（无 *draft* 占位）兼容：草稿追加在所有文件之后 */
    if (!hasMarker) {
        for (int k = nextDraft; k <= drafts; k++) {
            int di = Session_OpenDraft(draftdir, dnames[k - 1], k);
            docIdx[total++] = di;
            if (di >= 0) opened++;
        }
    }

    /* 清理未被任何标签引用的草稿文件（异常退出残留等） */
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

    /* 回到上次激活的标签：优先 activetab（位置序号），旧格式回退 activefile/activedraft */
    if (savedActiveTab >= 1 && savedActiveTab <= total && docIdx[savedActiveTab - 1] >= 0) {
        Editor_Activate(docIdx[savedActiveTab - 1]);
    } else if (savedActiveFile[0]) {
        int ai = Editor_FindDocByPath(savedActiveFile);
        if (ai >= 0) Editor_Activate(ai);
    } else if (!hasMarker) {
        int idx = nTabs + savedActiveDraft - 1;   /* 旧版草稿追加在文件之后 */
        if (savedActiveDraft >= 1 && idx >= 0 && idx < total && docIdx[idx] >= 0)
            Editor_Activate(docIdx[idx]);
    }
    return opened;
}
