#ifndef RESOURCE_H
#define RESOURCE_H

/* ----- Menus / Commands ----- */
#define IDR_MENU_MAIN   101
#define IDR_ACCEL       102
#define IDI_APP         103

/* File */
#define IDM_NEW         1100
#define IDM_OPEN        1101
#define IDM_SAVE        1102
#define IDM_SAVEAS      1103
#define IDM_CLOSE       1104
#define IDM_OPENENC     1105   /* open with encoding... */
#define IDM_RECENT_FIRST 1150
#define IDM_RECENT_LAST  1199
#define IDM_EXIT        1190

/* Edit */
#define IDM_UNDO        1200
#define IDM_REDO        1201
#define IDM_CUT         1202
#define IDM_COPY        1203
#define IDM_PASTE       1204
#define IDM_DELETE      1205
#define IDM_SELECTALL   1206
#define IDM_FIND        1210
#define IDM_REPLACE     1211
#define IDM_GOTO        1212
#define IDM_TIME        1213
#define IDM_FINDNEXT    1214
#define IDM_FINDPREV    1215
#define IDM_JSON_FMT    1216   /* pretty-print JSON */
#define IDM_JSON_MIN    1217   /* minify JSON */

/* View */
#define IDM_WRAP        1300
#define IDM_GUTTER      1301
#define IDM_ZOOMIN      1310
#define IDM_ZOOMOUT     1311
#define IDM_ZOOMRST     1312
#define IDM_THEME       1320

/* Encoding (save / current) */
#define IDM_ENC_ANSI     1400
#define IDM_ENC_UTF8     1401
#define IDM_ENC_UTF8BOM  1402
#define IDM_ENC_UTF16LE  1403
#define IDM_ENC_UTF16BE  1404
#define IDM_REOPEN_ANSI     1410
#define IDM_REOPEN_UTF8     1411
#define IDM_REOPEN_UTF8BOM  1412
#define IDM_REOPEN_UTF16LE  1413
#define IDM_REOPEN_UTF16BE  1414

/* EOL */
#define IDM_EOL_CRLF     1500
#define IDM_EOL_LF       1501
#define IDM_EOL_CR       1502
#define IDM_EOL_CONVERT  1510

/* Language */
#define IDM_LANG_FIRST   1600
#define IDM_LANG_NONE    1600
#define IDM_LANG_C       1601
#define IDM_LANG_CPP     1602
#define IDM_LANG_CS      1603
#define IDM_LANG_JAVA    1604
#define IDM_LANG_JS      1605
#define IDM_LANG_PY      1606
#define IDM_LANG_XML     1607
#define IDM_LANG_JSON    1608
#define IDM_LANG_SQL     1609
#define IDM_LANG_LAST    1609

/* Help */
#define IDM_ABOUT       1999

/* ----- Dialogs ----- */
#define IDD_FIND    2100
#define IDD_REPLACE 2101
#define IDD_GOTO    2102
#define IDD_ABOUT   2103
#define IDD_OPENENC 2104

/* Find/Replace controls */
#define IDC_FIND_TEXT   2201
#define IDC_REPL_TEXT   2202
#define IDC_FIND_CASE   2203
#define IDC_FIND_WORD   2204
#define IDC_FIND_DOWN   2205
#define IDC_FIND_NEXT   2206
#define IDC_FIND_PREV   2207
#define IDC_FIND_ALL    2208
#define IDC_FIND_REPL   2209
#define IDC_FIND_REPLALL 2210
#define IDC_FIND_CLOSE  2211
#define IDC_FIND_STATUS 2212

/* Goto */
#define IDC_GOTO_TEXT   2301
#define IDC_GOTO_OK     2302
#define IDC_GOTO_CANCEL 2303
#define IDC_GOTO_LABEL  2304

/* About */
#define IDC_ABOUT_TEXT  2401

/* Open encoding */
#define IDC_OPENENC_LIST 2501

#endif /* RESOURCE_H */
