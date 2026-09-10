#ifndef I18N_H
#define I18N_H

/* ---------- 界面多语言 ----------
   所有用户可见文字（菜单/对话框/消息框/状态栏）统一收进字符串表，
   经 T(STR_xxx) 取当前界面语言的文本。菜单在运行时由代码构建
   （I18n_BuildMainMenu），对话框沿用 .rc 模板、WM_INITDIALOG 时覆盖文字。
   界面语言保存于 session.ini [settings] uilang（0=中文，1=英文），
   未设置时首次启动跟随系统 UI 语言。 */

#include <windows.h>

/* 界面语言编号 */
enum { UI_LANG_ZH = 0, UI_LANG_EN = 1, UI_LANG_COUNT = 2 };

/* 字符串 ID（顺序必须与 i18n.c 中 kStr 两张表一致） */
typedef enum {
    /* 通用 */
    STR_APP_TITLE = 0,      /* 消息框标题 "ETON" */
    STR_UNTITLED,           /* 未命名 */
    STR_UNTITLED_N,         /* 未命名 %d */
    STR_OK,
    STR_CANCEL,
    STR_CLOSE,
    STR_ERROR,              /* 标题：错误 */

    /* 文件菜单 */
    STR_MENU_FILE,
    STR_ITEM_NEW,
    STR_ITEM_OPEN,
    STR_ITEM_OPENENC,
    STR_ITEM_SAVE,
    STR_ITEM_SAVEAS,
    STR_ITEM_CLOSE,
    STR_MENU_RECENT,
    STR_RECENT_EMPTY,
    STR_ITEM_EXIT,

    /* 编辑菜单 */
    STR_MENU_EDIT,
    STR_ITEM_UNDO,
    STR_ITEM_REDO,
    STR_ITEM_CUT,
    STR_ITEM_COPY,
    STR_ITEM_PASTE,
    STR_ITEM_DELETE,
    STR_ITEM_SELECTALL,
    STR_ITEM_FIND,
    STR_ITEM_REPLACE,
    STR_ITEM_GOTO,
    STR_ITEM_TIME,
    STR_ITEM_JSONFMT,
    STR_ITEM_JSONMIN,

    /* 视图菜单 */
    STR_MENU_VIEW,
    STR_ITEM_WRAP,
    STR_ITEM_GUTTER,
    STR_ITEM_ZOOMIN,
    STR_ITEM_ZOOMOUT,
    STR_ITEM_ZOOMRST,
    STR_ITEM_THEME,

    /* 编码菜单 */
    STR_MENU_ENC,
    STR_ENCNAME_ANSI,       /* 与状态栏/编码对话框共用 */
    STR_ENCNAME_UTF8,
    STR_ENCNAME_UTF8BOM,
    STR_ENCNAME_UTF16LE,
    STR_ENCNAME_UTF16BE,
    STR_REOPEN_FMT,         /* 以 %s 重新打开 */

    /* 行尾菜单 */
    STR_MENU_EOL,
    STR_EOLNAME_CRLF,
    STR_EOLNAME_LF,
    STR_EOLNAME_CR,
    STR_ITEM_EOLCONVERT,

    /* （语法）语言菜单 */
    STR_MENU_SYNTAX,
    STR_SYN_NONE,

    /* 帮助菜单 */
    STR_MENU_HELP,
    STR_ITEM_ABOUT,
    STR_MENU_UI,            /* 界面语言 */

    /* 文件对话框过滤器片段（配对使用） */
    STR_FILTER_ALL,
    STR_FILTER_ALLPAT,
    STR_FILTER_TEXTS,
    STR_FILTER_TEXTSPAT,
    STR_FILTER_TXT,
    STR_FILTER_TXTPAT,

    /* 查找 / 替换对话框 */
    STR_TITLE_FIND,
    STR_TITLE_REPLACE,
    STR_FIND_WHAT,
    STR_REPLACE_WITH,
    STR_MATCH_CASE,
    STR_WHOLE_WORD,
    STR_SEARCH_DOWN,
    STR_BTN_FIND_NEXT,
    STR_BTN_PREV,
    STR_BTN_REPLACE,
    STR_BTN_REPLACE_ALL,
    STR_ENTER_TEXT,
    STR_MATCH_FOUND,
    STR_NO_MATCH,
    STR_REPLACED,
    STR_REPLACED_N,

    /* 跳转到行 */
    STR_TITLE_GOTO,
    STR_GOTO_LBL,
    STR_GOTO_LBL_FMT,       /* 行号 (1 - %d): */

    /* 关于 */
    STR_TITLE_ABOUT,
    STR_ABOUT_L1,
    STR_ABOUT_L2,
    STR_ABOUT_L3,
    STR_ABOUT_VERSION,      /* 版本 %hs ... */

    /* 以指定编码打开 */
    STR_TITLE_OPENENC,
    STR_OPENENC_LBL,

    STR_UNTITLED_TXT,       /* 未命名.txt（另存为默认名） */

    /* 消息 */
    STR_MSG_NOT_FOUND,          /* 未找到匹配项。 */
    STR_MSG_FILE_NOT_SAVED,     /* 文件 "%s" 尚未保存... */
    STR_MSG_FILE_MODIFIED,      /* 文件 "%s" 已修改... */
    STR_MSG_SAVE_FAILED,
    STR_MSG_SCINTILLA_FAIL,
    STR_STATUS_SEL_CHARS,       /* 已选 %d 个字符 */
    STR_STATUS_CHARS,           /* 共 %d 个字符 */
    STR_STATUS_LN,              /* 第 %d 行 / 共 %d 行 */
    STR_STATUS_COL,             /* 列 %d */

    /* fileio */
    STR_MSG_CANT_OPEN,
    STR_MSG_CANT_WRITE,

    /* JSON */
    STR_JSON_TITLE_FMT,
    STR_JSON_TITLE_MIN,
    STR_JSON_EMPTY,
    STR_JSON_ERR_FMT,       /* 解析失败（行 %d，列 %d）：\n%s */
    STR_JERR_EMPTY,
    STR_JERR_EOF,
    STR_JERR_CHAR,
    STR_JERR_STRING_CTRL,
    STR_JERR_ESCAPE,
    STR_JERR_UNICODE,
    STR_JERR_NUMBER,
    STR_JERR_LITERAL,
    STR_JERR_KEY,
    STR_JERR_COLON,
    STR_JERR_COMMA_OBJ,
    STR_JERR_COMMA_ARR,
    STR_JERR_TRAILING,
    STR_JERR_DEPTH,
    STR_JERR_MEM,
    STR_JERR_UNKNOWN,

    /* 菜单/命令扩展（关闭其他、右键菜单、书签、正则） */
    STR_ITEM_CLOSE_OTHER,
    STR_ITEM_CLOSE_ALL,
    STR_ITEM_EXPLORER,
    STR_REGEX,
    STR_BM_TOGGLE,
    STR_BM_NEXT,
    STR_BM_PREV,

    /* 可靠性提示 */
    STR_TITLE_SAVE,              /* 标题：保存 */
    STR_MSG_ANSI_WARN,           /* ANSI 另存乱码警告 */
    STR_MSG_FILE_CHANGED_OVERWRITE, /* 保存时磁盘已变，是否覆盖 */
    STR_MSG_FILE_CHANGED_RELOAD,    /* 磁盘已变，是否重新加载 */
    STR_MSG_FILE_TOO_BIG,           /* 文件过大（%d MB 上限） */
    STR_MSG_AUTOBACKUP,             /* 发现自动备份，是否恢复 */

    /* 大文件流式 IO */
    STR_LOAD_ING,                   /* 进度框：正在加载 */
    STR_SAVE_ING,                   /* 进度框：正在保存 */
    STR_MSG_JSON_BIG,               /* JSON 工具大文档门槛 */

    /* Markdown 预览（mdview/mermaid） */
    STR_ITEM_MDPREVIEW,             /* 视图菜单：Markdown 预览 F12 */
    STR_SYN_MARKDOWN,               /* 语言菜单：Markdown（专有名词，中英一致） */
    STR_MD_MERMAID_UNSUP,           /* mermaid 图族暂不支持原生渲染的提示 */
    STR_ITEM_MDSPLIT,               /* 视图菜单：并排预览 Shift+F12 */
    STR_ITEM_EXPHTML,               /* 文件菜单：导出 HTML */
    STR_ITEM_EXPPDF,                /* 文件菜单：导出 PDF（打印） */

    STR_COUNT
} StrId;

/* 当前界面语言（0=中文 1=英文），启动时由 I18n_Init 确定 */
void I18n_Init(void);
int  I18n_Lang(void);
void I18n_SetLang(int lang);            /* 设置并写入 session.ini */

/* 取当前界面语言的字符串 */
const wchar_t* T(StrId id);

/* 代码构建整个主菜单（含语言子菜单）；同时刷新 g_hMenuRecent */
HMENU I18n_BuildMainMenu(void);

/* ---------- 菜单自绘（暗色主题下菜单栏/弹窗跟随配色） ----------
   菜单项以 MF_OWNERDRAW 追加，itemData = 文本指针；
   主窗口把 WM_MEASUREITEM / WM_DRAWITEM / WM_MENUCHAR 转发到这里。 */
void  I18n_OwnerAppend(HMENU m, UINT flags, UINT_PTR cmd, const wchar_t* text);
BOOL  I18n_OnMeasureItem(HWND hwnd, MEASUREITEMSTRUCT* mis);
BOOL  I18n_OnDrawItem(HWND hwnd, const DRAWITEMSTRUCT* dis);
int   I18n_FindMnemonic(HMENU menu, wchar_t ch);   /* WM_MENUCHAR 助记符定位 */
void  I18n_ApplyMenuTheme(HMENU root);             /* 菜单底色刷（含子菜单） */
void  I18n_RefreshMenuColors(void);                /* 按当前主题重建菜单刷 */
void  I18n_ApplySystemThemeMode(void);             /* 系统部件（消息框等）明暗跟随 */

/* 运行时覆盖对话框文字（dlgId 用 resource.h 的 IDD_*） */
void I18n_ApplyDialog(HWND hdlg, int dlgId);

/* 对话框 WM_CTLCOLOR* 统一处理：暗色主题下返回深色底/浅色字，
   浅色模式返回 NULL 走系统默认。对话框过程直接返回其结果。 */
HBRUSH I18n_DlgCtlColor(HWND hdlg, HDC hdc, UINT msg);

/* 拼接打开/保存对话框过滤器："名称1\0模式1\0名称2\0模式2\0\0" */
void I18n_JoinFilter(wchar_t* out, int cch,
                     StrId n1, StrId p1, StrId n2, StrId p2);

/* 最近文件子菜单（由 I18n_BuildMainMenu 填充，main.c 的
   RefreshRecentMenu 直接向其追加，不再按文字查找位置） */
extern HMENU g_hMenuRecent;

#endif /* I18N_H */
