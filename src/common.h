#ifndef COMMON_H
#define COMMON_H

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "Scintilla.h"
#include "Lexilla.h"
#include "resource.h"
#include "i18n.h"

/* custom window messages */
#define WM_APP_TABSEL      (WM_APP + 1)
#define WM_APP_TABCLOSE    (WM_APP + 2)
#define WM_APP_GUTUPDATE   (WM_APP + 3)
#define WM_APP_RECOLOR     (WM_APP + 4)

#define MAX_DOCS 64
#define MAX_RECENT 12

typedef enum {
    ENC_ANSI = 0,
    ENC_UTF8,
    ENC_UTF8_BOM,
    ENC_UTF16LE,
    ENC_UTF16BE
} Encoding;

typedef enum {
    LANG_NONE = 0,
    LANG_C, LANG_CPP, LANG_CS, LANG_JAVA, LANG_JS,
    LANG_PY, LANG_XML, LANG_JSON, LANG_SQL
} LangID;

typedef struct {
    HWND hwndEdit;       /* Scintilla window */
    wchar_t path[MAX_PATH];
    wchar_t title[256];      /* display title (may have " *" for dirty) */
    wchar_t baseTitle[256];  /* clean title without dirty marker */
    wchar_t draft[64];       /* 草稿文件名（仅未命名文档，空 = 无草稿） */
    Encoding enc;
    int eol;          /* 0=CRLF, 1=LF, 2=CR */
    BOOL dirty;
    BOOL isNew;
    LangID lang;
    FILETIME ftWrite;    /* 打开/保存时的磁盘时间戳（外部修改检测用） */
} Doc;

/* globals (defined in main.c) */
extern HINSTANCE g_hInst;
extern HWND g_hwndMain, g_hwndTab, g_hwndStatus;
extern UINT g_dpi;            /* 主窗口当前 DPI（96 基准，随 WM_DPICHANGED 更新） */
int UI_Scale(int px);         /* 96-DPI 基准像素值 → 当前 DPI 实际像素 */
extern Doc g_docs[MAX_DOCS];
extern int g_docCount;
extern int g_curDoc;
extern HFONT g_hFont;
extern int g_fontSize;       /* points */
extern BOOL g_wordWrap;
extern BOOL g_showGutter;
extern BOOL g_dark;
extern int g_gutterWidth;
extern wchar_t g_recent[MAX_RECENT][MAX_PATH];
extern int g_recentCount;

extern COLORREF g_clrBg, g_clrFg, g_clrSelBg, g_clrGutterBg, g_clrGutterFg,
               g_clrStatusBg, g_clrStatusFg;
extern BOOL g_suppressDirty;
extern BOOL g_draftsDirty;

/* editor.c */
BOOL Editor_Init(void);
void Editor_ApplyThemeColors(void);
int  Editor_NewDoc(const wchar_t* path, const wchar_t* title, BOOL isNew);
void Editor_CloseDoc(int index);
void Editor_Activate(int index);
void Editor_Layout(void);
void Editor_SetFont(void);
void Editor_OnDpiChanged(void);
void Editor_Zoom(int delta);
void Editor_ApplyTheme(int index);
void Editor_SetLang(int index, LangID lang);
void Editor_OnLanguageChanged(void);   /* 界面语言切换后刷新未命名标题与状态栏 */
void Editor_MarkDirty(int index, BOOL dirty);
void Editor_UpdateStatus(void);
HWND Editor_ActiveEdit(void);
void Editor_UpdateGutter(int index);
void Editor_OnNotify(LPARAM lp);
BOOL Editor_SaveDoc(int index, BOOL askPath);
BOOL Editor_SaveDocAs(int index);
void Editor_UpdateTitle(int index);
int  Editor_FindDocByPath(const wchar_t* path);
int  Editor_IndexFromHwnd(HWND hed);
void Editor_ApplyWrap(int index);
void Editor_ConvertEol(int index, int eol);
BOOL Editor_LoadFile(int index, const wchar_t* path, Encoding enc);
void Editor_ComputeGutterWidth(int index);
void Editor_SetText(int index, const char* utf8);
char* Editor_GetTextUtf8(int index, DWORD* outLen);
void Editor_ToggleBookmark(int index);
void Editor_GotoBookmark(int index, BOOL next);
void OpenFileByPath(const wchar_t* path);

/* fileio.c */
const wchar_t* EncodingName(Encoding e);
Encoding DetectEncoding(const BYTE* data, DWORD size, BOOL* hasBom);
wchar_t* LoadFileToWStr(const wchar_t* path, Encoding enc, DWORD* outLenChars, int* outEol);
/* Scintilla uses UTF-8; this loads file bytes and returns a UTF-8 string + length */
char* LoadFileToUtf8(const wchar_t* path, Encoding* detectedEnc, int* outEol);
BOOL SaveUtf8ToFile(const wchar_t* path, const char* text, DWORD len, Encoding enc, int eol);
BOOL HasUnsupportedForAnsi(const wchar_t* text, DWORD len);
wchar_t* ApplyEol(const wchar_t* text, DWORD len, int eol, DWORD* outLen);

/* dialogs.c */
extern wchar_t g_findText[512];
extern wchar_t g_replText[512];
extern BOOL g_findCase, g_findWord, g_findDown, g_findRegex;
extern LONG g_findStart;
extern HWND g_hFindDlg;   /* 非模态查找/替换对话框（空 = 未打开） */
void Dlg_Find(HWND hwnd, BOOL replace);
void Dlg_Goto(HWND hwnd);
void Dlg_About(HWND hwnd);
void Dlg_OpenEnc(HWND hwnd);
void Find_MarkAll(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL re);
LONG DoFindFrom(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL re,
                BOOL down, LONG start, BOOL wrap, BOOL* found);

/* jsonfmt.c */
void Json_FormatActiveDoc(BOOL minify);

/* util */
void ShowError(const wchar_t* msg);
void AddRecent(const wchar_t* path);
void RefreshRecentMenu(void);
void ApplyTitleBarTheme(HWND hwnd);   /* 按 g_dark 设置深/浅标题栏 */

/* tabbar.c */
void TabBar_Register(void);
void TabBar_ApplyTheme(void);   /* tooltip 深/浅主题跟随 */
int  TabBar_HitTestPublic(HWND hwnd, int mx, int my, BOOL* closeHit, int* arrow);

/* statusbar.c（自绘状态栏，颜色随主题） */
void StatusBar_Register(void);
void StatusBar_SetParts(const int* rightEdges, int n);
BOOL StatusBar_SetText(int part, const wchar_t* text);

/* scrollbar.c（自绘滚动条，替代 Scintilla 经典滚动条） */
void ScrollBars_Create(HWND parent);
int  ScrollBars_Thickness(void);
void ScrollBars_Layout(int x, int y, int w, int h, int thickness, BOOL showH);
void ScrollBars_Update(void);
void ScrollBars_Repaint(void);

/* editor.c */
BOOL DiskWriteTimePublic(const wchar_t* path, FILETIME* ft);

/* session.c */
BOOL Session_IniPath(wchar_t* out, DWORD cch);   /* %APPDATA%\eton\session.ini */
void Session_Save(void);
void Session_SaveDrafts(void);
void Session_DiscardDraft(int index);
int  Session_Restore(void);
void Settings_Load(void);   /* [settings]：主题/换行/行号/字号（启动时） */
void Settings_Save(void);   /* 同上（退出时） */
/* 正式文件的定时自动备份（防崩溃丢内容，%APPDATA%\eton\autoback\） */
void Session_AutoBackupWrite(int index);
void Session_AutoBackupDiscard(const wchar_t* path);
BOOL Session_AutoBackupExists(const wchar_t* path);
BOOL Session_AutoBackupRead(const wchar_t* path, char** out, DWORD* len);

#endif /* COMMON_H */
