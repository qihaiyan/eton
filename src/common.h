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
#define ENC_AUTO (-1)

typedef BOOL (*IoProgressFn)(UINT64 done, UINT64 total, void* ctx);

typedef enum {
    LANG_NONE = 0,
    LANG_C, LANG_CPP, LANG_CS, LANG_JAVA, LANG_JS,
    LANG_PY, LANG_XML, LANG_JSON, LANG_SQL,
    LANG_MD
} LangID;

typedef struct {
    HWND hwndEdit;
    wchar_t path[MAX_PATH];
    wchar_t title[256];
    wchar_t baseTitle[256];
    wchar_t draft[64];
    Encoding enc;
    int eol;
    BOOL dirty;
    BOOL isNew;
    LangID lang;
    FILETIME ftWrite;
    BOOL previewOn;
} Doc;

extern HINSTANCE g_hInst;
extern HWND g_hwndMain, g_hwndTab, g_hwndStatus;
extern UINT g_dpi;
int UI_Scale(int px);
extern Doc g_docs[MAX_DOCS];
extern int g_docCount;
extern int g_curDoc;
extern HFONT g_hFont;
extern int g_fontSize;
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
extern BOOL g_mdSplit;
extern BOOL g_mdSyncLock;

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
void Editor_OnLanguageChanged(void);
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

const wchar_t* EncodingName(Encoding e);
Encoding DetectEncoding(const BYTE* data, DWORD size, BOOL* hasBom);
wchar_t* LoadFileToWStr(const wchar_t* path, Encoding enc, DWORD* outLenChars, int* outEol);
char* LoadFileToUtf8(const wchar_t* path, Encoding* detectedEnc, int* outEol);
BOOL SaveUtf8ToFile(const wchar_t* path, const char* text, DWORD len, Encoding enc, int eol);
BOOL StreamLoadToDoc(HWND hed, const wchar_t* path, Encoding enc,
                     Encoding* outEnc, int* outEol,
                     IoProgressFn progress, void* ctx);
BOOL StreamSaveFromDoc(HWND hed, const wchar_t* path, Encoding enc, int eol,
                       IoProgressFn progress, void* ctx);
BOOL HasUnsupportedForAnsi(const wchar_t* text, DWORD len);
wchar_t* ApplyEol(const wchar_t* text, DWORD len, int eol, DWORD* outLen);

extern wchar_t g_findText[512];
extern wchar_t g_replText[512];
extern BOOL g_findCase, g_findWord, g_findDown, g_findRegex;
extern LONG g_findStart;
extern HWND g_hFindDlg;
void Dlg_Find(HWND hwnd, BOOL replace);
void Dlg_Goto(HWND hwnd);
void Dlg_About(HWND hwnd);
void Dlg_OpenEnc(HWND hwnd);
void Find_MarkAll(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL re);
LONG DoFindFrom(HWND hed, const wchar_t* text, BOOL cs, BOOL ww, BOOL re,
                BOOL down, LONG start, BOOL wrap, BOOL* found);

void Json_FormatActiveDoc(BOOL minify);

typedef struct MdFonts {
    HFONT body, bold, emph, boldemph;
    HFONT h[6];
    HFONT mono;
    HFONT sm;
    HFONT mathIt, mathUp, mathItS, mathUpS, mathItSS, mathUpSS;
    int  lineH;
} MdFonts;

typedef struct MdTheme {
    COLORREF bg, fg, fgMuted;
    COLORREF head;
    COLORREF link;
    COLORREF quoteBar;
    COLORREF codeBg, codeBorder, codeFg;
    COLORREF tableLine, tableHeadBg;
    COLORREF hrule;
    COLORREF selBg;
    COLORREF merNodeFill, merNodeBorder, merNodeText, merEdge;
    COLORREF merLabelBg, merLabelFg;
    COLORREF seqNoteBg, seqNoteBorder, seqNoteFg;
    COLORREF frame;
} MdTheme;

typedef struct MermaidDiagram MermaidDiagram;
MermaidDiagram* Mermaid_Parse(const char* src, int len);
void  Mermaid_Free(MermaidDiagram* d);
SIZE  Mermaid_Measure(MermaidDiagram* d, HDC hdc, const MdFonts* f);
void  Mermaid_Draw(MermaidDiagram* d, HDC hdc, int x, int y,
                   const MdFonts* f, const MdTheme* th);

typedef struct MathBox MathBox;
MathBox* Math_Build(const wchar_t* latex);
void  Math_Free(MathBox* b);
void  Math_Measure(MathBox* b, HDC hdc, const MdFonts* f);
void  Math_Draw(const MathBox* b, HDC hdc, int x, int yBase,
                const MdTheme* th, const MdFonts* f);
int   Math_Width(const MathBox* b);
int   Math_Height(const MathBox* b);
int   Math_Ascent(const MathBox* b);

void MdView_Register(void);
void MdView_Create(HWND parent);
void MdView_Toggle(void);
void MdView_ToggleSplit(void);
void MdView_OnActivate(void);
void MdView_OnLayout(int x, int y, int w, int h);
BOOL MdView_IsVisible(void);
void MdView_OnDpiChanged(void);
void MdView_OnThemeChange(void);
void MdView_OnZoom(void);
void MdView_RefreshIfActive(int index);
void MdView_SyncScrollFromEdit(double frac);
double MdView_GetScrollFraction(void);
void MdTheme_Build(MdTheme* th);

void Editor_SyncScrollFromPreview(double frac);

void MdExport_Html(void);
void MdExport_Pdf(void);
BOOL MdView_PrintPages(HDC hdc, int printableW, int printableH);


void ShowError(const wchar_t* msg);
void AddRecent(const wchar_t* path);
void RefreshRecentMenu(void);
void ApplyTitleBarTheme(HWND hwnd);

void TabBar_Register(void);
void TabBar_ApplyTheme(void);
int  TabBar_HitTestPublic(HWND hwnd, int mx, int my, BOOL* closeHit, int* arrow);

void StatusBar_Register(void);
void StatusBar_SetParts(const int* rightEdges, int n);
BOOL StatusBar_SetText(int part, const wchar_t* text);

void ScrollBars_Create(HWND parent);
int  ScrollBars_Thickness(void);
void ScrollBars_Layout(int x, int y, int w, int h, int thickness, BOOL showH);
void ScrollBars_Update(void);
void ScrollBars_Repaint(void);

BOOL DiskWriteTimePublic(const wchar_t* path, FILETIME* ft);

BOOL Session_IniPath(wchar_t* out, DWORD cch);
void Session_Save(void);
void Session_SaveDrafts(void);
void Session_DiscardDraft(int index);
int  Session_Restore(void);
void Session_SaveWindow(HWND hwnd);
int  Session_RestoreWindow(HWND hwnd, int nShowCmd);
void Settings_Load(void);
void Settings_Save(void);
void Session_AutoBackupWrite(int index);
void Session_AutoBackupDiscard(const wchar_t* path);
BOOL Session_AutoBackupExists(const wchar_t* path);
BOOL Session_AutoBackupRead(const wchar_t* path, char** out, DWORD* len);

#endif
