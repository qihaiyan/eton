/*
 * mdimg.c — Markdown 预览的远程图片获取与缓存
 *
 * 布局在 UI 线程同步进行，网络请求不能阻塞：首次遇到 http(s) 图片时
 * MdImg_Ensure() 派生工作线程下载到 %TEMP%\eton_mdimg，完成后向预览
 * 窗口投递 WM_MDIMG_READY，由 mdview.c 强制重排。
 * 同一 URL 会话内只尝试一次（成功缓存 / 失败记为不再重试），
 * 下载先写 .part 再改名，保证缓存文件始终完整。
 */
#include "common.h"
#include <winhttp.h>

/* 任务状态：0 空闲 1 下载中 2 已缓存 3 失败（本会话不再重试） */
typedef struct { wchar_t url[2048]; volatile LONG st; } MdImgJob;
#define MDIMG_MAX_JOBS 48

static MdImgJob s_jobs[MDIMG_MAX_JOBS];
static CRITICAL_SECTION s_cs;
static BOOL s_csOk = FALSE;
static HWND s_notify = NULL;
static wchar_t s_dir[MAX_PATH];

#define MDIMG_MAX_BYTES (8 * 1024 * 1024)   /* 单图 8MB 上限 */
#define MDIMG_MAX_REDIRECTS 5

/* FNV-1a 64：URL → 缓存文件名（十六进制） */
static unsigned __int64 MdImgUrlHash(const wchar_t* url) {
    unsigned __int64 h = 1469598103934665603ULL;
    for (const wchar_t* p = url; *p; p++) {
        h ^= (unsigned __int64)*p;
        h *= 1099511628211ULL;
    }
    return h;
}

static BOOL UrlExt(const wchar_t* url, wchar_t* ext, int cch) {
    const wchar_t* slash = wcsrchr(url, L'/');
    const wchar_t* dot = wcsrchr(url, L'.');
    if (!dot || (slash && dot < slash)) return FALSE;
    int n = 0;
    for (const wchar_t* p = dot + 1; *p && n < cch - 2; p++) {
        wchar_t c = *p;
        if (c == L'?' || c == L'#') break;
        if (c >= L'A' && c <= L'Z') c += 32;
        if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'))) break;
        ext[n++] = c;
    }
    if (n == 0) return FALSE;
    ext[n] = L'\0';
    return TRUE;
}

BOOL MdImg_CachePath(const wchar_t* url, wchar_t* out, int cch) {
    if (!url || !*url || !s_dir[0]) { out[0] = L'\0'; return FALSE; }
    wchar_t ext[8] = L".img";
    wchar_t raw[8];
    if (UrlExt(url, raw, 8)) {
        ext[0] = L'.';
        wcscpy_s(ext + 1, 7, raw);
    }
    int n = swprintf_s(out, (size_t)cch, L"%s\\%016llx%s",
                       s_dir, MdImgUrlHash(url), ext);
    return n > 0;
}

/* 启动时清理两周前的缓存，避免无限增长 */
static void PurgeOld(void) {
    if (!s_dir[0]) return;
    ULARGE_INTEGER cutoff;
    GetSystemTimeAsFileTime((FILETIME*)&cutoff);
    cutoff.QuadPart -= (ULONGLONG)14 * 24 * 3600 * 10000000;
    wchar_t pat[MAX_PATH + 4];
    if (swprintf_s(pat, MAX_PATH + 4, L"%s\\*", s_dir) <= 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER ft;
        ft.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        ft.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (ft.QuadPart < cutoff.QuadPart) {
            wchar_t full[MAX_PATH + 4];
            if (swprintf_s(full, MAX_PATH + 4, L"%s\\%s", s_dir, fd.cFileName) > 0)
                DeleteFileW(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void MdImg_Init(HWND notifyWnd) {
    s_notify = notifyWnd;
    InitializeCriticalSection(&s_cs);
    s_csOk = TRUE;
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) { s_dir[0] = L'\0'; return; }
    if (swprintf_s(s_dir, MAX_PATH, L"%seton_mdimg", tmp) <= 0) { s_dir[0] = L'\0'; return; }
    CreateDirectoryW(s_dir, NULL);
    PurgeOld();
}

/* 单次请求：跟随 3xx 跳转（上限 MDIMG_MAX_REDIRECTS），200 才落盘 */
static BOOL DownloadOne(const wchar_t* url) {
    wchar_t cur[2048];
    wcsncpy_s(cur, 2048, url, _TRUNCATE);
    wchar_t finalPath[MAX_PATH];
    if (!MdImg_CachePath(url, finalPath, MAX_PATH)) return FALSE;

    for (int hop = 0; hop <= MDIMG_MAX_REDIRECTS; hop++) {
        URL_COMPONENTSW uc;
        ZeroMemory(&uc, sizeof(uc));
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256], path[1800], extra[512];
        host[0] = path[0] = extra[0] = L'\0';
        uc.lpszHostName = host;    uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path;     uc.dwUrlPathLength = 1800;
        uc.lpszExtraInfo = extra;  uc.dwExtraInfoLength = 512;
        if (!WinHttpCrackUrl(cur, 0, 0, &uc)) return FALSE;

        wchar_t obj[2300];
        if (swprintf_s(obj, 2300, L"%s%s", path, extra) <= 0) return FALSE;

        BOOL ok = FALSE;
        HINTERNET hs = NULL, hc = NULL, hr = NULL;
        HANDLE hf = INVALID_HANDLE_VALUE;
        hs = WinHttpOpen(L"eton/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hs) return FALSE;
        WinHttpSetTimeouts(hs, 5000, 5000, 10000, 20000);
        hc = WinHttpConnect(hs, host, uc.nPort, 0);
        if (hc) {
            hr = WinHttpOpenRequest(hc, L"GET", obj, NULL, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    uc.nScheme == INTERNET_SCHEME_HTTPS
                                        ? WINHTTP_FLAG_SECURE : 0);
            if (hr && WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hr, NULL)) {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(hr,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    NULL, &status, &sz, NULL);
                if (status >= 300 && status < 400) {
                    wchar_t loc[2048]; loc[0] = L'\0';
                    sz = 2048;
                    if (WinHttpQueryHeaders(hr, WINHTTP_QUERY_LOCATION,
                                            NULL, loc, &sz, NULL) &&
                        loc[0]) {
                        wchar_t next[2048];
                        if (_wcsnicmp(loc, L"http://", 7) == 0 ||
                            _wcsnicmp(loc, L"https://", 8) == 0) {
                            wcsncpy_s(next, 2048, loc, _TRUNCATE);
                        } else if (loc[0] == L'/') {
                            /* 站内绝对路径：保留原 scheme://host:port */
                            wchar_t scheme[16];
                            scheme[0] = L'\0';
                            URL_COMPONENTSW u2;
                            ZeroMemory(&u2, sizeof(u2));
                            u2.dwStructSize = sizeof(u2);
                            u2.lpszScheme = scheme; u2.dwSchemeLength = 16;
                            WinHttpCrackUrl(cur, 0, 0, &u2);
                            if (swprintf_s(next, 2048, L"%s://%s:%d%s",
                                           scheme, host, (int)uc.nPort, loc) <= 0)
                                next[0] = L'\0';
                        } else {
                            next[0] = L'\0';
                        }
                        if (next[0]) {
                            if (hr) WinHttpCloseHandle(hr);
                            if (hc) WinHttpCloseHandle(hc);
                            WinHttpCloseHandle(hs);
                            wcsncpy_s(cur, 2048, next, _TRUNCATE);
                            continue;
                        }
                    }
                } else if (status == 200) {
                    wchar_t part[MAX_PATH + 8];
                    if (swprintf_s(part, MAX_PATH + 8, L"%s.part", finalPath) > 0) {
                        hf = CreateFileW(part, GENERIC_WRITE, 0, NULL,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hf != INVALID_HANDLE_VALUE) {
                            ok = TRUE;
                            for (;;) {
                                DWORD avail = 0;
                                if (!WinHttpQueryDataAvailable(hr, &avail)) { ok = FALSE; break; }
                                if (avail == 0) break;
                                char buf[65536];
                                DWORD chunk = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
                                DWORD got = 0;
                                if (!WinHttpReadData(hr, buf, chunk, &got) || got == 0) {
                                    ok = FALSE; break;
                                }
                                DWORD pos = 0, written = 0;
                                while (pos < got) {
                                    if (!WriteFile(hf, buf + pos, got - pos, &written, NULL) ||
                                        written == 0) { ok = FALSE; break; }
                                    pos += written;
                                }
                                if (!ok) break;
                                if (GetFileSize(hf, NULL) > MDIMG_MAX_BYTES ||
                                    GetFileSize(hf, NULL) == INVALID_FILE_SIZE) {
                                    ok = FALSE; break;
                                }
                            }
                            CloseHandle(hf); hf = INVALID_HANDLE_VALUE;
                            if (ok) {
                                ok = MoveFileExW(part, finalPath,
                                                 MOVEFILE_REPLACE_EXISTING) != 0;
                            } else {
                                DeleteFileW(part);
                            }
                        }
                    }
                }
            }
        }
        if (hr) WinHttpCloseHandle(hr);
        if (hc) WinHttpCloseHandle(hc);
        if (hs) WinHttpCloseHandle(hs);
        if (hf != INVALID_HANDLE_VALUE) CloseHandle(hf);
        return ok;
    }
    return FALSE;
}

static DWORD WINAPI FetchThread(LPVOID p) {
    wchar_t* url = (wchar_t*)p;
    BOOL ok = DownloadOne(url);

    int slot = -1;
    if (s_csOk) {
        EnterCriticalSection(&s_cs);
        for (int i = 0; i < MDIMG_MAX_JOBS; i++)
            if (wcscmp(s_jobs[i].url, url) == 0) { slot = i; break; }
        if (slot >= 0) s_jobs[slot].st = ok ? 2 : 3;
        LeaveCriticalSection(&s_cs);
    }
    free(url);
    if (ok && s_notify)
        PostMessageW(s_notify, WM_MDIMG_READY, 0, 0);
    return 0;
}

void MdImg_Ensure(const wchar_t* url) {
    if (!s_csOk || !url || !*url) return;
    if (_wcsnicmp(url, L"http://", 7) != 0 && _wcsnicmp(url, L"https://", 8) != 0)
        return;

    int slot = -1;
    EnterCriticalSection(&s_cs);
    for (int i = 0; i < MDIMG_MAX_JOBS; i++) {
        if (wcscmp(s_jobs[i].url, url) == 0) {
            if (s_jobs[i].st == 0) { s_jobs[i].st = 1; slot = i; }
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < MDIMG_MAX_JOBS; i++)
            if (s_jobs[i].st == 0) { slot = i; break; }
        if (slot < 0)
            for (int i = 0; i < MDIMG_MAX_JOBS; i++)
                if (s_jobs[i].st != 1) { slot = i; break; }
        if (slot >= 0) {
            wcsncpy_s(s_jobs[slot].url, 2048, url, _TRUNCATE);
            s_jobs[slot].st = 1;
        }
    }
    LeaveCriticalSection(&s_cs);
    if (slot < 0) return;

    wchar_t* copy = _wcsdup(url);
    HANDLE h = copy ? CreateThread(NULL, 0, FetchThread, copy, 0, NULL) : NULL;
    if (!h) {
        free(copy);
        EnterCriticalSection(&s_cs);
        s_jobs[slot].st = 0;
        LeaveCriticalSection(&s_cs);
    } else {
        CloseHandle(h);
    }
}
