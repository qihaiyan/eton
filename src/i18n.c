#include "common.h"
#include "version.h"

/* ---------- 界面多语言 ----------
   kStr[语言][STR_xxx] 两张字符串表；表行顺序必须与 i18n.h 的 StrId 枚举一致。
   新增文字：在枚举末尾（STR_COUNT 前）加 ID，并在两张表同一位置加译文。
   菜单不再使用 .rc 资源，全部由此处按当前语言构建，切换语言时重建。 */

static int g_lang = UI_LANG_ZH;
HMENU g_hMenuRecent = NULL;

static const wchar_t* const kStr[UI_LANG_COUNT][STR_COUNT] = {

    /* ================= 简体中文 ================= */
    {
        /* 通用 */
        L"ETON",
        L"未命名",
        L"未命名 %d",
        L"确定",
        L"取消",
        L"关闭",
        L"错误",

        /* 文件菜单 */
        L"文件(&F)",
        L"新建(&N)\tCtrl+N",
        L"打开(&O)...\tCtrl+O",
        L"以指定编码打开...",
        L"保存(&S)\tCtrl+S",
        L"另存为(&A)...",
        L"关闭标签(&W)\tCtrl+W",
        L"最近文件",
        L"(空)",
        L"退出(&X)",

        /* 编辑菜单 */
        L"编辑(&E)",
        L"撤销(&U)\tCtrl+Z",
        L"重做(&R)\tCtrl+Y",
        L"剪切(&T)\tCtrl+X",
        L"复制(&C)\tCtrl+C",
        L"粘贴(&P)\tCtrl+V",
        L"删除(&D)\tDel",
        L"全选(&A)\tCtrl+A",
        L"查找(&F)...\tCtrl+F",
        L"替换(&H)...\tCtrl+H",
        L"跳转到行(&G)...\tCtrl+G",
        L"插入日期/时间(&I)",
        L"JSON 格式化(&J)\tCtrl+Shift+F",
        L"JSON 压缩(&M)\tCtrl+Shift+M",

        /* 视图菜单 */
        L"视图(&V)",
        L"自动换行(&W)",
        L"显示行号(&L)",
        L"放大(&+)\tCtrl++",
        L"缩小(&-)\tCtrl+-",
        L"重置缩放(&0)\tCtrl+0",
        L"深色主题(&D)",

        /* 编码菜单 */
        L"编码(&N)",
        L"ANSI",
        L"UTF-8",
        L"UTF-8 (带 BOM)",
        L"UTF-16 LE",
        L"UTF-16 BE",
        L"以 %s 重新打开",

        /* 行尾菜单 */
        L"行尾(&E)",
        L"Windows (CRLF)",
        L"Unix (LF)",
        L"Mac (CR)",
        L"转换行尾为当前格式",

        /* （语法）语言菜单 */
        L"语言(&L)",
        L"纯文本",

        /* 帮助菜单 */
        L"帮助(&H)",
        L"关于(&A)",
        L"界面语言(&U)",

        /* 过滤器 */
        L"所有文件 (*.*)",
        L"*.*",
        L"文本文件 (*.txt;*.c;*.cpp;*.h;*.py;*.js;*.json;*.xml;*.html;*.sql)",
        L"*.txt;*.c;*.cpp;*.h;*.py;*.js;*.json;*.xml;*.html;*.sql",
        L"文本文件 (*.txt)",
        L"*.txt",

        /* 查找 / 替换 */
        L"查找",
        L"替换",
        L"查找内容:",
        L"替换为:",
        L"区分大小写",
        L"全词匹配",
        L"向下",
        L"查找下一个",
        L"上一个",
        L"替换",
        L"全部替换",
        L"请输入查找内容",
        L"已找到匹配项",
        L"未找到匹配项",
        L"已替换",
        L"已替换 %d 处",

        /* 跳转到行 */
        L"跳转到行",
        L"行号:",
        L"行号 (1 - %d):",

        /* 关于 */
        L"关于 ETON",
        L"ETON — 轻量级文本编辑器",
        L"基于原生 Win32 API (C) 实现，零第三方依赖。",
        L"支持多标签、语法高亮、编码转换、查找替换等。",
        L"版本 %hs  |  基于 Visual Studio 生成工具构建",

        /* 以指定编码打开 */
        L"以指定编码打开",
        L"选择编码:",

        L"未命名.txt",

        /* 消息 */
        L"未找到匹配项。",
        L"文件 \"%s\" 尚未保存，是否保存？",
        L"文件 \"%s\" 已修改，是否保存后再关闭？",
        L"保存失败。",
        L"创建 Scintilla 控件失败。",
        L"已选 %d 个字符",
        L"共 %d 个字符",
        L"第 %d 行 / 共 %d 行",
        L"列 %d",

        /* fileio */
        L"无法打开文件。",
        L"无法写入文件。",

        /* JSON */
        L"JSON 格式化",
        L"JSON 压缩",
        L"当前内容为空，没有可处理的 JSON。",
        L"JSON 解析失败（第 %d 行，第 %d 列）：\n%s",
        L"内容为空，未找到 JSON 值。",
        L"JSON 不完整（意外结束）。",
        L"无效字符，此处应为 JSON 值（注意是否有多余的逗号）。",
        L"字符串中包含未转义的控制字符。",
        L"无效的转义序列。",
        L"\\u 转义后应跟随 4 位十六进制数。",
        L"无效的数字格式。",
        L"无效的字面量（应为 true / false / null）。",
        L"对象的键必须是字符串（注意是否有多余的逗号）。",
        L"键后应跟随 ':'。",
        L"期望 ',' 或 '}'。",
        L"期望 ',' 或 ']'。",
        L"JSON 值结束后存在多余内容。",
        L"嵌套层级过深。",
        L"内存不足。",
        L"未知错误。",

        L"关闭其他标签(&O)",
        L"关闭所有标签(&L)",
        L"打开所在文件夹(&O)",
        L"正则表达式(&R)",
        L"切换书签(&K)\tCtrl+F2",
        L"下一个书签(&N)\tF2",
        L"上一个书签(&P)\tShift+F2",

        L"保存",
        L"文件中包含无法用 ANSI（系统代码页）表示的字符，按 ANSI 保存可能产生乱码。\n仍要以 ANSI 保存吗？",
        L"文件 \"%s\" 已在磁盘上被其他程序修改，覆盖保存可能丢失这些修改。\n仍要保存吗？",
        L"文件 \"%s\" 已在磁盘上被其他程序修改。\n是否重新加载？",
        L"文件过大（超过 %d MB），无法打开。",
        L"检测到 \"%s\" 存在未保存的自动备份（可能来自上次异常退出）。\n是否恢复备份内容？",

        L"正在加载",
        L"正在保存",
        L"文档过大（超过 %d MB），JSON 工具仅支持较小的文档。",
    },

    /* ================= English ================= */
    {
        /* generic */
        L"ETON",
        L"Untitled",
        L"Untitled %d",
        L"OK",
        L"Cancel",
        L"Close",
        L"Error",

        /* File menu */
        L"&File",
        L"&New\tCtrl+N",
        L"&Open...\tCtrl+O",
        L"Open with E&ncoding...",
        L"&Save\tCtrl+S",
        L"Save &As...",
        L"Clo&se Tab\tCtrl+W",
        L"&Recent Files",
        L"(Empty)",
        L"E&xit",

        /* Edit menu */
        L"&Edit",
        L"&Undo\tCtrl+Z",
        L"&Redo\tCtrl+Y",
        L"Cu&t\tCtrl+X",
        L"&Copy\tCtrl+C",
        L"&Paste\tCtrl+V",
        L"&Delete\tDel",
        L"Select &All\tCtrl+A",
        L"&Find...\tCtrl+F",
        L"&Replace...\tCtrl+H",
        L"&Go to Line...\tCtrl+G",
        L"Insert Date/&Time",
        L"Format &JSON\tCtrl+Shift+F",
        L"Mini&fy JSON\tCtrl+Shift+M",

        /* View menu */
        L"&View",
        L"&Word Wrap",
        L"Show &Line Numbers",
        L"Zoom &In\tCtrl++",
        L"Zoom &Out\tCtrl+-",
        L"&Reset Zoom\tCtrl+0",
        L"&Dark Theme",

        /* Encoding menu */
        L"En&coding",
        L"ANSI",
        L"UTF-8",
        L"UTF-8 (BOM)",
        L"UTF-16 LE",
        L"UTF-16 BE",
        L"Reopen as %s",

        /* Line endings menu */
        L"&Line Endings",
        L"Windows (CRLF)",
        L"Unix (LF)",
        L"Mac (CR)",
        L"Convert All to Current",

        /* (syntax) Language menu */
        L"&Language",
        L"Plain Text",

        /* Help menu */
        L"&Help",
        L"&About ETON",
        L"&UI Language",

        /* Filters */
        L"All files (*.*)",
        L"*.*",
        L"Text files (*.txt;*.c;*.cpp;*.h;*.py;*.js;*.json;*.xml;*.html;*.sql)",
        L"*.txt;*.c;*.cpp;*.h;*.py;*.js;*.json;*.xml;*.html;*.sql",
        L"Text files (*.txt)",
        L"*.txt",

        /* Find / Replace */
        L"Find",
        L"Replace",
        L"Fi&nd what:",
        L"Re&place with:",
        L"Match &case",
        L"Match &whole word",
        L"&Down",
        L"Find &Next",
        L"&Previous",
        L"&Replace",
        L"Replace &All",
        L"Enter text to find",
        L"Match found",
        L"No matches found",
        L"Replaced",
        L"Replaced %d occurrence(s)",

        /* Go to line */
        L"Go to Line",
        L"Line &number:",
        L"Line (1 - %d):",

        /* About */
        L"About ETON",
        L"ETON — a lightweight text editor",
        L"Built with the native Win32 API in C, no third-party runtime.",
        L"Multi-tab editing, syntax highlighting, encoding conversion, find && replace, and more.",
        L"Version %hs  |  Built with the Visual Studio Build Tools",

        /* Open with encoding */
        L"Open with Encoding",
        L"Choose &encoding:",

        L"Untitled.txt",

        /* Messages */
        L"No matches found.",
        L"File \"%s\" has not been saved. Save it now?",
        L"File \"%s\" has been modified. Save before closing?",
        L"Failed to save the file.",
        L"Failed to create the Scintilla control.",
        L"%d characters selected",
        L"%d characters",
        L"Line %d / %d",
        L"Col %d",

        /* fileio */
        L"Cannot open the file.",
        L"Cannot write the file.",

        /* JSON */
        L"Format JSON",
        L"Minify JSON",
        L"The document is empty — nothing to process.",
        L"JSON parse failed (line %d, column %d):\n%s",
        L"The content is empty; no JSON value found.",
        L"Incomplete JSON (unexpected end of input).",
        L"Invalid character: expected a JSON value here (check for a trailing comma).",
        L"Unescaped control character inside a string.",
        L"Invalid escape sequence.",
        L"Expected 4 hex digits after the \\u escape.",
        L"Invalid number format.",
        L"Invalid literal (expected true / false / null).",
        L"Object keys must be strings (check for a trailing comma).",
        L"Expected ':' after the key.",
        L"Expected ',' or '}'.",
        L"Expected ',' or ']'.",
        L"Unexpected content after the JSON value.",
        L"Nesting too deep.",
        L"Out of memory.",
        L"Unknown error.",

        L"Close &Other Tabs",
        L"Close A&ll Tabs",
        L"Open Containing &Folder",
        L"Regular &expression",
        L"Toggle Book&mark\tCtrl+F2",
        L"&Next Bookmark\tF2",
        L"&Previous Bookmark\tShift+F2",

        L"Save",
        L"The file contains characters that cannot be represented in ANSI (the system code page); saving as ANSI may corrupt them.\nSave as ANSI anyway?",
        L"File \"%s\" has been changed on disk by another program. Saving may overwrite those changes.\nSave anyway?",
        L"File \"%s\" has been changed on disk by another program.\nReload it?",
        L"The file is too large (over %d MB) and cannot be opened.",
        L"An unsaved auto-backup of \"%s\" was found (possibly from an abnormal exit).\nRestore the backup content?",

        L"Loading",
        L"Saving",
        L"The document is too large (over %d MB) for the JSON tools.",
    },
};

const wchar_t* T(StrId id) {
    if ((int)id < 0 || (int)id >= STR_COUNT) return L"";
    return kStr[g_lang][id];
}

/* ---------- 系统绘制部件的明暗跟随 ----------
   uxtheme 序号 135 = SetPreferredAppMode（Win10 1809+）：让系统绘制的
   菜单箭头/弹窗边框/消息框跟随应用主题（ForceDark/ForceLight），
   失败（老系统）时静默保持系统默认。 */
void I18n_ApplySystemThemeMode(void) {
    typedef int (WINAPI *SetPreferredAppModeFn)(int);
    static SetPreferredAppModeFn fn;
    static BOOL tried = FALSE;
    if (!tried) {
        HMODULE ux = LoadLibraryW(L"uxtheme.dll");
        if (ux) fn = (SetPreferredAppModeFn)(void*)GetProcAddress(ux, (LPCSTR)135);
        tried = TRUE;
    }
    if (fn) fn(g_dark ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
}

/* 弹出菜单窗口（#32768）应用暗色主题：让系统在自绘条目之上画的
   子菜单箭头变成浅色（WM_INITMENUPOPUP 时调用，每个弹窗各一次）。 */

/* ---------- 语言选择与持久化 ---------- */
void I18n_Init(void) {
    wchar_t ini[MAX_PATH];
    if (Session_IniPath(ini, MAX_PATH)) {
        int v = (int)GetPrivateProfileIntW(L"settings", L"uilang", 0xFFFF, ini);
        if (v == UI_LANG_ZH || v == UI_LANG_EN) { g_lang = v; return; }
    }
    /* 未设置过：跟随系统 UI 语言（中文系统用中文，其余用英文） */
    g_lang = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE)
           ? UI_LANG_ZH : UI_LANG_EN;
    /* 注意：I18n_ApplySystemThemeMode 需在 Settings_Load 置好 g_dark 后调用（WinMain） */
}

int I18n_Lang(void) {
    return g_lang;
}

void I18n_SetLang(int lang) {
    if (lang != UI_LANG_ZH && lang != UI_LANG_EN) return;
    if (lang == g_lang) return;
    g_lang = lang;
    wchar_t ini[MAX_PATH];
    if (Session_IniPath(ini, MAX_PATH)) {
        wchar_t v[2] = { L'0' + (wchar_t)lang, L'\0' };
        WritePrivateProfileStringW(L"settings", L"uilang", v, ini);
    }
}

/* ---------- 主菜单（代码构建，替代 .rc 菜单资源） ---------- */
static void AddIt(HMENU m, StrId s, UINT cmd) {
    I18n_OwnerAppend(m, MF_STRING, cmd, T(s));
}
static void AddSep(HMENU m) {
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
}
/* 菜单栏条目：指针打 bit0 标记，测量/绘制按紧凑样式处理 */
static void AddBar(HMENU bar, UINT_PTR sub, StrId s) {
    I18n_OwnerAppend(bar, MF_POPUP, sub,
                     (const wchar_t*)(uintptr_t)((uintptr_t)T(s) | 1));
}

/* 菜单自绘：底色刷按主题重建 */
static HBRUSH s_menuBrush = NULL;
static int s_menuBrushDark = -1;

void I18n_RefreshMenuColors(void) {
    if (s_menuBrush && s_menuBrushDark == g_dark) return;
    if (s_menuBrush) DeleteObject(s_menuBrush);
    s_menuBrush = CreateSolidBrush(g_dark ? RGB(43,43,43) : RGB(242,242,242));
    s_menuBrushDark = g_dark;
}

void I18n_ApplyMenuTheme(HMENU root) {
    I18n_RefreshMenuColors();
    /* 底色刷同时应用到所有子菜单（项间空隙也用主题底色） */
    MENUINFO mi; memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = s_menuBrush;
    SetMenuInfo(root, &mi);
}

static HFONT MenuFont(void) {
    static HFONT font = NULL;
    if (!font) {
        NONCLIENTMETRICSW ncm; memset(&ncm, 0, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        font = CreateFontIndirectW(&ncm.lfMenuFont);
    }
    return font;
}

void I18n_OwnerAppend(HMENU m, UINT flags, UINT_PTR cmd, const wchar_t* text) {
    AppendMenuW(m, (flags & ~MF_OWNERDRAW) | MF_OWNERDRAW, cmd, (LPCVOID)text);
}

/* 拆分 "文本\t加速键" 两段。
   注意：_snwprintf 在输出恰好等于 count 时不写 NUL，必须用 "%.*s" 保证终止，
   否则标签后带栈垃圾，菜单项重绘（滚轮移动高亮）时会画出乱字符。 */
static void SplitAccel(const wchar_t* text, wchar_t* label, int lcch, wchar_t* accel, int acch) {
    label[0] = accel[0] = L'\0';
    if (!text) return;
    const wchar_t* tab = wcschr(text, L'\t');
    if (!tab) {
        _snwprintf(label, lcch, L"%s", text);
    } else {
        _snwprintf(label, lcch, L"%.*s", (int)(tab - text), text);
        _snwprintf(accel, acch, L"%s", tab + 1);
    }
    label[lcch - 1] = L'\0';
    accel[acch - 1] = L'\0';
}

BOOL I18n_OnMeasureItem(HWND hwnd, MEASUREITEMSTRUCT* mis) {
    if (mis->CtlType != ODT_MENU) return FALSE;
    /* 指针 bit0 = 菜单栏条目标记（无勾选槽、更紧凑） */
    uintptr_t raw = (uintptr_t)mis->itemData;
    int isBar = (int)(raw & 1);
    const wchar_t* text = (const wchar_t*)(raw & ~(uintptr_t)1);
    wchar_t label[200], accel[64];
    SplitAccel(text, label, 200, accel, 64);
    HDC hdc = GetDC(hwnd);
    HFONT old = (HFONT)SelectObject(hdc, MenuFont());
    /* 用 DT_CALCRECT 量宽：与绘制一致地吃掉 & 助记符前缀，
       GetTextExtentPoint32W 会把 & 本身也计入导致条目偏宽 */
    RECT rl = {0, 0, 0, 0}, ra = {0, 0, 0, 0};
    DrawTextW(hdc, label, -1, &rl, DT_CALCRECT | DT_SINGLELINE);
    if (accel[0]) DrawTextW(hdc, accel, -1, &ra, DT_CALCRECT | DT_SINGLELINE);
    int lw = rl.right - rl.left, lh = rl.bottom - rl.top;
    int aw = ra.right - ra.left;
    SelectObject(hdc, old); ReleaseDC(hwnd, hdc);
    if (isBar) {
        mis->itemHeight = lh + 7;
        mis->itemWidth  = 2 * UI_Scale(4) + lw;   /* 两侧各 4px，紧凑 */
        return TRUE;
    }
    int gutter = lh + 6;   /* 勾选标记区 */
    mis->itemHeight = lh + 9;
    mis->itemWidth  = 10 + gutter + lw + (accel[0] ? aw + 20 : 0) + 14;
    /* bit1 = 有子菜单：为系统绘制的子菜单箭头预留宽度 */
    if (mis->itemData && ((uintptr_t)mis->itemData & 2))
        mis->itemWidth += UI_Scale(14);
    UINT maxw = (UINT)UI_Scale(560);   /* 超长路径（最近文件）限制菜单宽度，绘制时省略号截断 */
    if (mis->itemWidth > maxw) mis->itemWidth = maxw;
    return TRUE;
}

BOOL I18n_OnDrawItem(HWND hwnd, const DRAWITEMSTRUCT* dis) {
    (void)hwnd;
    if (dis->CtlType != ODT_MENU) return FALSE;
    uintptr_t raw = (uintptr_t)dis->itemData;
    int isBar = (int)(raw & 1);
    const wchar_t* text = (const wchar_t*)(raw & ~(uintptr_t)1);
    if (!text) text = L"";
    BOOL selected = (dis->itemState & ODS_SELECTED) != 0;
    BOOL disabled = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    BOOL checked  = (dis->itemState & ODS_CHECKED) != 0;
    HDC hdc = dis->hDC;
    COLORREF bg = selected ? RGB(0,120,215)
                           : (g_dark ? RGB(43,43,43) : RGB(242,242,242));
    COLORREF fg = disabled ? (g_dark ? RGB(130,130,130) : RGB(150,150,150))
                           : selected ? RGB(255,255,255)
                           : (g_dark ? RGB(238,238,238) : RGB(20,20,20));
    COLORREF dim = disabled ? fg
                           : selected ? RGB(230,240,255)
                           : (g_dark ? RGB(160,160,160) : RGB(110,110,110));
    HBRUSH b = CreateSolidBrush(bg);
    FillRect(hdc, &dis->rcItem, b);
    DeleteObject(b);
    HFONT old = (HFONT)SelectObject(hdc, MenuFont());
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    wchar_t label[200], accel[64];
    SplitAccel(text, label, 200, accel, 64);
    SIZE ls; GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &ls);
    int gutter = isBar ? 0 : ls.cy + 6;
    if ((raw & 2) && g_dark) {
        /* 有子菜单：右缘垫一块稍亮的底衬，系统绘制的黑色箭头落在其上才可见 */
        RECT chip = { dis->rcItem.right - UI_Scale(20), dis->rcItem.top + 1,
                      dis->rcItem.right - 2, dis->rcItem.bottom - 1 };
        HBRUSH cb = CreateSolidBrush(RGB(96, 96, 96));
        FillRect(hdc, &chip, cb);
        DeleteObject(cb);
    }
    if (checked) {   /* 勾选符号：细线小号 ✓，紧贴文字（参考 Notepad++ 样式） */
        int tx = dis->rcItem.left + 6 + gutter;              /* 文字起点 */
        int gh = ls.cy + 6;
        int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
        POINT pts[3] = {
            { tx - gh * 80 / 100, cy - gh *  3 / 100 },
            { tx - gh * 50 / 100, cy + gh *  17 / 100 },
            { tx - gh * 20 / 100, cy - gh *  23 / 100 },
        };
        LOGBRUSH lb; memset(&lb, 0, sizeof(lb));
        lb.lbStyle = BS_SOLID; lb.lbColor = fg;
        int pw = gh / 15 > 1 ? gh / 15 : 1;
        HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                                pw, &lb, 0, NULL);
        HPEN oldp = (HPEN)SelectObject(hdc, pen);
        Polyline(hdc, pts, 3);
        SelectObject(hdc, oldp);
        DeleteObject(pen);
    }
    RECT lr = { dis->rcItem.left + 6 + gutter, dis->rcItem.top,
                dis->rcItem.right - 8 - (isBar ? gutter : 0), dis->rcItem.bottom };
    if (isBar) lr.left = dis->rcItem.left + UI_Scale(4);
    DrawTextW(hdc, label, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (accel[0]) {
        SetTextColor(hdc, dim);
        RECT ar = { dis->rcItem.right - 210, dis->rcItem.top, dis->rcItem.right - 8, dis->rcItem.bottom };
        DrawTextW(hdc, accel, -1, &ar, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, old);
    return TRUE;
}

/* WM_MENUCHAR：owner-draw 菜单的助记符（Alt+F 等）由这里定位到项 */
int I18n_FindMnemonic(HMENU menu, wchar_t ch) {
    int n = GetMenuItemCount(menu);
    wchar_t up = (wchar_t)towupper(ch);
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW mii; memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_DATA | MIIM_FTYPE;
        if (!GetMenuItemInfoW(menu, i, TRUE, &mii)) continue;
        if (mii.fType & MFT_SEPARATOR) continue;
        const wchar_t* text = (const wchar_t*)mii.dwItemData;
        if (!text) continue;
        for (const wchar_t* p = text; *p; p++) {
            if (*p == L'&' && p[1] && p[1] != L'&') {
                if (towupper(p[1]) == up) return i;
                break;   /* 只认第一个助记符 */
            }
        }
    }
    return -1;
}

HMENU I18n_BuildMainMenu(void) {
    HMENU bar = CreateMenu();
    if (!bar) return NULL;

    /* 文件 */
    HMENU mFile = CreatePopupMenu();
    AddIt(mFile, STR_ITEM_NEW, IDM_NEW);
    AddIt(mFile, STR_ITEM_OPEN, IDM_OPEN);
    AddIt(mFile, STR_ITEM_OPENENC, IDM_OPENENC);
    AddIt(mFile, STR_ITEM_SAVE, IDM_SAVE);
    AddIt(mFile, STR_ITEM_SAVEAS, IDM_SAVEAS);
    AddIt(mFile, STR_ITEM_CLOSE, IDM_CLOSE);
    AddIt(mFile, STR_ITEM_CLOSE_OTHER, IDM_CLOSE_OTHER);
    AddIt(mFile, STR_ITEM_CLOSE_ALL, IDM_CLOSE_ALL);
    AddSep(mFile);
    /* 最近文件子菜单（条目由 RefreshRecentMenu 运行时填充）。
       指针 bit1 = 有子菜单：绘制时在右缘垫浅色底衬，让系统画的箭头可见。 */
    g_hMenuRecent = CreatePopupMenu();
    I18n_ApplyMenuTheme(g_hMenuRecent);
    I18n_OwnerAppend(g_hMenuRecent, MF_STRING | MF_GRAYED, IDM_RECENT_FIRST, T(STR_RECENT_EMPTY));
    I18n_OwnerAppend(mFile, MF_POPUP, (UINT_PTR)g_hMenuRecent,
                     (const wchar_t*)((uintptr_t)T(STR_MENU_RECENT) | 2));
    AddSep(mFile);
    AddIt(mFile, STR_ITEM_EXIT, IDM_EXIT);
    AddBar(bar, (UINT_PTR)mFile, STR_MENU_FILE);

    /* 编辑 */
    HMENU mEdit = CreatePopupMenu();
    AddIt(mEdit, STR_ITEM_UNDO, IDM_UNDO);
    AddIt(mEdit, STR_ITEM_REDO, IDM_REDO);
    AddSep(mEdit);
    AddIt(mEdit, STR_ITEM_CUT, IDM_CUT);
    AddIt(mEdit, STR_ITEM_COPY, IDM_COPY);
    AddIt(mEdit, STR_ITEM_PASTE, IDM_PASTE);
    AddIt(mEdit, STR_ITEM_DELETE, IDM_DELETE);
    AddSep(mEdit);
    AddIt(mEdit, STR_ITEM_SELECTALL, IDM_SELECTALL);
    AddSep(mEdit);
    AddIt(mEdit, STR_ITEM_FIND, IDM_FIND);
    AddIt(mEdit, STR_ITEM_REPLACE, IDM_REPLACE);
    AddIt(mEdit, STR_ITEM_GOTO, IDM_GOTO);
    AddSep(mEdit);
    AddIt(mEdit, STR_BM_TOGGLE, IDM_BM_TOGGLE);
    AddIt(mEdit, STR_BM_NEXT, IDM_BM_NEXT);
    AddIt(mEdit, STR_BM_PREV, IDM_BM_PREV);
    AddSep(mEdit);
    AddIt(mEdit, STR_ITEM_TIME, IDM_TIME);
    AddSep(mEdit);
    AddIt(mEdit, STR_ITEM_JSONFMT, IDM_JSON_FMT);
    AddIt(mEdit, STR_ITEM_JSONMIN, IDM_JSON_MIN);
    AddBar(bar, (UINT_PTR)mEdit, STR_MENU_EDIT);

    /* 视图 */
    HMENU mView = CreatePopupMenu();
    AddIt(mView, STR_ITEM_WRAP, IDM_WRAP);
    AddIt(mView, STR_ITEM_GUTTER, IDM_GUTTER);
    AddSep(mView);
    AddIt(mView, STR_ITEM_ZOOMIN, IDM_ZOOMIN);
    AddIt(mView, STR_ITEM_ZOOMOUT, IDM_ZOOMOUT);
    AddIt(mView, STR_ITEM_ZOOMRST, IDM_ZOOMRST);
    AddSep(mView);
    AddIt(mView, STR_ITEM_THEME, IDM_THEME);
    AddBar(bar, (UINT_PTR)mView, STR_MENU_VIEW);

    /* 编码 */
    HMENU mEnc = CreatePopupMenu();
    for (int i = 0; i <= ENC_UTF16BE; i++)
        I18n_OwnerAppend(mEnc, MF_STRING, IDM_ENC_ANSI + i, EncodingName((Encoding)i));
    AddSep(mEnc);
    {
        /* owner-draw 不复制文本：存进静态缓冲，指向的内容在菜单存续期内有效 */
        static wchar_t s_reopen[5][80];
        for (int i = 0; i <= ENC_UTF16BE; i++) {
            wsprintf(s_reopen[i], T(STR_REOPEN_FMT), EncodingName((Encoding)i));
            I18n_OwnerAppend(mEnc, MF_STRING, IDM_REOPEN_ANSI + i, s_reopen[i]);
        }
    }
    AddBar(bar, (UINT_PTR)mEnc, STR_MENU_ENC);

    /* 行尾 */
    HMENU mEol = CreatePopupMenu();
    AddIt(mEol, STR_EOLNAME_CRLF, IDM_EOL_CRLF);
    AddIt(mEol, STR_EOLNAME_LF, IDM_EOL_LF);
    AddIt(mEol, STR_EOLNAME_CR, IDM_EOL_CR);
    AddSep(mEol);
    AddIt(mEol, STR_ITEM_EOLCONVERT, IDM_EOL_CONVERT);
    AddBar(bar, (UINT_PTR)mEol, STR_MENU_EOL);

    /* （语法）语言：各语言名称为专有名词，中英文一致 */
    HMENU mLang = CreatePopupMenu();
    AddIt(mLang, STR_SYN_NONE, IDM_LANG_NONE);
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_C, L"C");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_CPP, L"C++");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_CS, L"C#");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_JAVA, L"Java");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_JS, L"JavaScript");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_PY, L"Python");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_XML, L"XML/HTML");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_JSON, L"JSON");
    I18n_OwnerAppend(mLang, MF_STRING, IDM_LANG_SQL, L"SQL");
    AddBar(bar, (UINT_PTR)mLang, STR_MENU_SYNTAX);

    /* 帮助 */
    HMENU mHelp = CreatePopupMenu();
    AddIt(mHelp, STR_ITEM_ABOUT, IDM_ABOUT);
    AddSep(mHelp);
    /* 界面语言直接放入帮助菜单（避免子菜单箭头） */
    I18n_OwnerAppend(mHelp, MF_STRING, IDM_UI_LANG_ZH, L"简体中文");
    I18n_OwnerAppend(mHelp, MF_STRING, IDM_UI_LANG_EN, L"English");
    AddBar(bar, (UINT_PTR)mHelp, STR_MENU_HELP);

    I18n_ApplyMenuTheme(bar);
    return bar;
}

/* ---------- 对话框文字覆盖 ---------- */
typedef struct { int ctl; StrId str; } DlgEnt;

static const DlgEnt kDlgFind[] = {
    { IDC_FIND_LBL_TEXT, STR_FIND_WHAT },
    { IDC_FIND_CASE,     STR_MATCH_CASE },
    { IDC_FIND_WORD,     STR_WHOLE_WORD },
    { IDC_FIND_DOWN,     STR_SEARCH_DOWN },
    { IDC_FIND_REGEX,    STR_REGEX },
    { IDC_FIND_NEXT,     STR_BTN_FIND_NEXT },
    { IDC_FIND_PREV,     STR_BTN_PREV },
    { IDC_FIND_CLOSE,    STR_CLOSE },
    { 0, (StrId)0 }
};
static const DlgEnt kDlgReplace[] = {
    { IDC_FIND_LBL_TEXT,   STR_FIND_WHAT },
    { IDC_REPL_LBL_TEXT,   STR_REPLACE_WITH },
    { IDC_FIND_CASE,       STR_MATCH_CASE },
    { IDC_FIND_WORD,       STR_WHOLE_WORD },
    { IDC_FIND_DOWN,       STR_SEARCH_DOWN },
    { IDC_FIND_REGEX,      STR_REGEX },
    { IDC_FIND_NEXT,       STR_BTN_FIND_NEXT },
    { IDC_FIND_REPL,       STR_BTN_REPLACE },
    { IDC_FIND_REPLALL,    STR_BTN_REPLACE_ALL },
    { IDC_FIND_CLOSE,      STR_CLOSE },
    { 0, (StrId)0 }
};
static const DlgEnt kDlgGoto[] = {
    { IDC_GOTO_LABEL,  STR_GOTO_LBL },
    { IDC_GOTO_OK,     STR_OK },
    { IDC_GOTO_CANCEL, STR_CANCEL },
    { 0, (StrId)0 }
};
static const DlgEnt kDlgAbout[] = {
    { IDC_ABOUT_LINE1, STR_ABOUT_L1 },
    { IDC_ABOUT_LINE2, STR_ABOUT_L2 },
    { IDC_ABOUT_LINE3, STR_ABOUT_L3 },
    { IDC_ABOUT_LINE4, STR_ABOUT_VERSION },   /* 版本号在下方特例填入 */
    { IDC_GOTO_OK,     STR_OK },
    { 0, (StrId)0 }
};
static const DlgEnt kDlgOpenEnc[] = {
    { IDC_OPENENC_LBL, STR_OPENENC_LBL },
    { IDC_GOTO_OK,     STR_OK },
    { IDC_GOTO_CANCEL, STR_CANCEL },
    { 0, (StrId)0 }
};

static BOOL CALLBACK DarkBtnProc(HWND h, LPARAM lp);   /* 见下：按钮暗色主题 */

void I18n_ApplyDialog(HWND hdlg, int dlgId) {
    const DlgEnt* e = NULL;
    StrId cap = STR_APP_TITLE;
    switch (dlgId) {
        case IDD_FIND:    e = kDlgFind;    cap = STR_TITLE_FIND;    break;
        case IDD_REPLACE: e = kDlgReplace; cap = STR_TITLE_REPLACE; break;
        case IDD_GOTO:    e = kDlgGoto;    cap = STR_TITLE_GOTO;    break;
        case IDD_ABOUT:   e = kDlgAbout;   cap = STR_TITLE_ABOUT;   break;
        case IDD_OPENENC: e = kDlgOpenEnc; cap = STR_TITLE_OPENENC; break;
        default: return;
    }
    SetWindowTextW(hdlg, T(cap));
    ApplyTitleBarTheme(hdlg);   /* 对话框标题栏跟随主题（DWM 暗色属性） */
    for (; e->ctl; e++) {
        if (dlgId == IDD_ABOUT && e->ctl == IDC_ABOUT_LINE4) continue;
        SetDlgItemTextW(hdlg, e->ctl, T(e->str));
    }
    if (dlgId == IDD_ABOUT) {
        wchar_t buf[160];
        wsprintf(buf, T(STR_ABOUT_VERSION), ET_VERSION_STR);
        SetDlgItemTextW(hdlg, IDC_ABOUT_LINE4, buf);
    }
    if (dlgId == IDD_GOTO) {
        /* 行号范围带文档行数，由 GotoProc 在其后自行覆盖 */
    }
    if (g_dark)
        EnumChildWindows(hdlg, DarkBtnProc, 0);
}

/* 按钮类控件（含复选框）换用系统暗色主题类，浅色模式保持默认 */
static BOOL CALLBACK DarkBtnProc(HWND h, LPARAM lp) {
    typedef BOOL (WINAPI *SetThemeFn)(HWND, LPCWSTR, LPCWSTR);
    static SetThemeFn st;
    if (!st) {
        HMODULE ux = LoadLibraryW(L"uxtheme.dll");
        if (ux) st = (SetThemeFn)(void*)GetProcAddress(ux, "SetWindowTheme");
    }
    wchar_t cls[32];
    if (st && GetClassNameW(h, cls, 32) && wcscmp(cls, L"Button") == 0)
        st(h, L"DarkMode_Explorer", NULL);
    return TRUE;
}

/* ---------- 对话框底色/文字颜色（暗色主题） ---------- */
static HBRUSH s_dlgBrush = NULL;
static int s_dlgBrushDark = -1;

HBRUSH I18n_DlgCtlColor(HWND hdlg, HDC hdc, UINT msg) {
    (void)hdlg;
    if (!g_dark) return NULL;   /* 浅色模式交回系统默认 */
    SetTextColor(hdc, RGB(238, 238, 238));
    SetBkColor(hdc, RGB(32, 32, 32));
    SetBkMode(hdc, TRANSPARENT);
    if (!s_dlgBrush || s_dlgBrushDark != g_dark) {
        if (s_dlgBrush) DeleteObject(s_dlgBrush);
        s_dlgBrush = CreateSolidBrush(RGB(32, 32, 32));
        s_dlgBrushDark = g_dark;
    }
    return s_dlgBrush;
}

/* ---------- 过滤器拼接 ---------- */
void I18n_JoinFilter(wchar_t* out, int cch,
                     StrId n1, StrId p1, StrId n2, StrId p2) {
    const wchar_t* parts[4] = { T(n1), T(p1), T(n2), T(p2) };
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        int l = (int)wcslen(parts[i]) + 1;
        if (pos + l > cch - 1) break;
        memcpy(out + pos, parts[i], (size_t)l * sizeof(wchar_t));
        pos += l;
    }
    out[pos] = L'\0';   /* 与末串的 '\0' 组成双 0 结尾 */
}
