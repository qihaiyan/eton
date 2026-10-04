/* etonctx.c — MSIX/商店版右键菜单的 IExplorerCommand COM 组件
 *
 * 经 AppxManifest 的 desktop4:FileExplorerContextMenus + com:ComServer
 * (SurrogateServer, dllhost 加载本 DLL) 注册，对 .md/.markdown 显示
 * "用 Eton 编辑"。标题跟随系统 UI 语言（zh → 中文，其余 → 英文），
 * 图标取同目录 eton.exe，Invoke 对每个选中文件启动一次 eton。
 *
 * 独立组件：不引用 common.h/工程内其它源文件，仅用系统头与 CRT。
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <initguid.h>   /* 在 shobjidl 前包含：GUID 就地定义，免链 uuid.lib */
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>

/* {1D5C4E8A-72B3-4F09-9A26-5E80C3B7D4F1} 类 CLSID（清单中引用） */
DEFINE_GUID(CLSID_EtonCtx, 0x1d5c4e8a, 0x72b3, 0x4f09, 0x9a, 0x26, 0x5e, 0x80, 0xc3, 0xb7, 0xd4, 0xf1);
/* {6A2F9D34-8C15-4B27-A3E8-D09F52C14B76} 命令规范名 */
DEFINE_GUID(GUID_EtonEditCmd, 0x6a2f9d34, 0x8c15, 0x4b27, 0xa3, 0xe8, 0xd0, 0x9f, 0x52, 0xc1, 0x4b, 0x76);

static const wchar_t* TitleText(void) {
    return (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE)
         ? L"用 Eton 编辑" : L"Edit with Eton";
}

/* 本 DLL 所在目录（包安装根）下的 eton.exe */
static void EtonExePath(wchar_t* out, DWORD cch) {
    HMODULE self = NULL;
    out[0] = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void*)&EtonExePath, &self) && self) {
        GetModuleFileNameW(self, out, cch);
        wchar_t* slash = wcsrchr(out, L'\\');
        if (slash) _snwprintf(slash + 1, cch - (DWORD)(slash + 1 - out), L"eton.exe");
        out[cch - 1] = 0;
    }
}

static HRESULT DupW(const wchar_t* s, LPWSTR* out) {
    return SHStrDupW(s, out);
}

/* ---------------- IExplorerCommand ---------------- */

typedef struct {
    IExplorerCommandVtbl* lpVtbl;
    LONG refs;
} EtonCmd;

static HRESULT STDMETHODCALLTYPE Cmd_QueryInterface(IExplorerCommand* p, REFIID riid, void** ppv) {
    EtonCmd* c = (EtonCmd*)p;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IExplorerCommand)) {
        *ppv = p;
        IUnknown_AddRef((IUnknown*)c);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Cmd_AddRef(IExplorerCommand* p) {
    return InterlockedIncrement(&((EtonCmd*)p)->refs);
}
static ULONG STDMETHODCALLTYPE Cmd_Release(IExplorerCommand* p) {
    LONG r = InterlockedDecrement(&((EtonCmd*)p)->refs);
    if (r == 0) HeapFree(GetProcessHeap(), 0, p);
    return (ULONG)r;
}

static HRESULT STDMETHODCALLTYPE Cmd_GetTitle(IExplorerCommand* p, IShellItemArray* arr, LPWSTR* name) {
    (void)p; (void)arr;
    if (!name) return E_POINTER;
    return DupW(TitleText(), name);
}
static HRESULT STDMETHODCALLTYPE Cmd_GetIcon(IExplorerCommand* p, IShellItemArray* arr, LPWSTR* icon) {
    (void)p; (void)arr;
    if (!icon) return E_POINTER;
    wchar_t exe[MAX_PATH], buf[MAX_PATH + 8];
    EtonExePath(exe, MAX_PATH);
    if (!exe[0]) return E_NOTIMPL;
    _snwprintf(buf, MAX_PATH + 8, L"%s,0", exe);
    buf[MAX_PATH + 7] = 0;
    return DupW(buf, icon);
}
static HRESULT STDMETHODCALLTYPE Cmd_GetToolTip(IExplorerCommand* p, IShellItemArray* arr, LPWSTR* tip) {
    (void)p; (void)arr;
    if (!tip) return E_POINTER;
    return DupW(TitleText(), tip);
}
static HRESULT STDMETHODCALLTYPE Cmd_GetCanonicalName(IExplorerCommand* p, GUID* guid) {
    (void)p;
    if (!guid) return E_POINTER;
    *guid = GUID_EtonEditCmd;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE Cmd_GetState(IExplorerCommand* p, IShellItemArray* arr, BOOL slow, EXPCMDSTATE* st) {
    (void)p; (void)arr; (void)slow;
    if (!st) return E_POINTER;
    *st = ECS_ENABLED;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE Cmd_Invoke(IExplorerCommand* p, IShellItemArray* arr, IBindCtx* bc) {
    (void)p; (void)bc;
    if (!arr) return S_OK;
    wchar_t exe[MAX_PATH];
    EtonExePath(exe, MAX_PATH);
    if (!exe[0]) return E_FAIL;
    DWORD n = 0;
    if (FAILED(IShellItemArray_GetCount(arr, &n))) return S_OK;
    for (DWORD i = 0; i < n; i++) {
        IShellItem* si = NULL;
        if (FAILED(IShellItemArray_GetItemAt(arr, i, &si)) || !si) continue;
        PWSTR path = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(si, SIGDN_FILESYSPATH, &path)) && path) {
            wchar_t args[4096];
            _snwprintf(args, 4096, L"\"%s\"", path);
            args[4095] = 0;
            ShellExecuteW(NULL, L"open", exe, args, NULL, SW_SHOWNORMAL);
            CoTaskMemFree(path);
        }
        IShellItem_Release(si);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE Cmd_GetFlags(IExplorerCommand* p, EXPCMDFLAGS* flags) {
    (void)p;
    if (!flags) return E_POINTER;
    *flags = ECF_DEFAULT;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE Cmd_EnumSubCommands(IExplorerCommand* p, IEnumExplorerCommand** en) {
    (void)p;
    if (en) *en = NULL;
    return E_NOTIMPL;
}

static IExplorerCommandVtbl g_cmdVtbl = {
    Cmd_QueryInterface, Cmd_AddRef, Cmd_Release,
    Cmd_GetTitle, Cmd_GetIcon, Cmd_GetToolTip, Cmd_GetCanonicalName,
    Cmd_GetState, Cmd_Invoke, Cmd_GetFlags, Cmd_EnumSubCommands
};

static HRESULT CmdCreate(REFIID riid, void** ppv) {
    EtonCmd* c = (EtonCmd*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EtonCmd));
    if (!c) return E_OUTOFMEMORY;
    c->lpVtbl = &g_cmdVtbl;
    c->refs = 1;
    HRESULT hr = IExplorerCommand_QueryInterface((IExplorerCommand*)c, riid, ppv);
    IExplorerCommand_Release((IExplorerCommand*)c);
    return hr;
}

/* ---------------- IClassFactory ---------------- */

typedef struct {
    IClassFactoryVtbl* lpVtbl;
} EtonFactory;

static HRESULT STDMETHODCALLTYPE Fac_QueryInterface(IClassFactory* p, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppv = p;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Fac_AddRef(IClassFactory* p)  { (void)p; return 2; }
static ULONG STDMETHODCALLTYPE Fac_Release(IClassFactory* p) { (void)p; return 1; }
static HRESULT STDMETHODCALLTYPE Fac_CreateInstance(IClassFactory* p, IUnknown* outer, REFIID riid, void** ppv) {
    (void)p;
    if (outer) return CLASS_E_NOAGGREGATION;
    return CmdCreate(riid, ppv);
}
static HRESULT STDMETHODCALLTYPE Fac_LockServer(IClassFactory* p, BOOL lock) {
    (void)p; (void)lock;
    return S_OK;
}

static IClassFactoryVtbl g_facVtbl = {
    Fac_QueryInterface, Fac_AddRef, Fac_Release,
    Fac_CreateInstance, Fac_LockServer
};
static EtonFactory g_factory = { &g_facVtbl };

/* ---------------- 导出 ---------------- */

/* 导出经链接器 /EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow 声明 */

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!IsEqualCLSID(rclsid, &CLSID_EtonCtx)) return CLASS_E_CLASSNOTAVAILABLE;
    return IClassFactory_QueryInterface((IClassFactory*)&g_factory, riid, ppv);
}

STDAPI DllCanUnloadNow(void) {
    return S_FALSE;   /* 交给 surrogate 进程生命周期管理 */
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
