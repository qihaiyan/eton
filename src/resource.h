#ifndef RESOURCE_H
#define RESOURCE_H

/* ----- Menus / Commands ----- */
/* 主菜单不再使用资源（菜单文字随界面语言切换，由 i18n.c 运行时构建） */
#define IDR_ACCEL       102
#define IDI_APP         103

/* File */
#define IDM_NEW         1100
#define IDM_OPEN        1101
#define IDM_SAVE        1102
#define IDM_SAVEAS      1103
#define IDM_CLOSE       1104
#define IDM_OPENENC     1105   /* open with encoding... */
#define IDM_CLOSE_OTHER 1106   /* 关闭其他标签 */
#define IDM_CLOSE_ALL   1107   /* 关闭所有标签 */
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
#define IDM_NEXT_TAB    1218   /* Ctrl+Tab 下一个标签 */
#define IDM_PREV_TAB    1219   /* Ctrl+Shift+Tab 上一个标签 */
#define IDM_BM_TOGGLE   1220   /* 书签：切换 */
#define IDM_BM_NEXT     1221   /* 书签：下一个 */
#define IDM_BM_PREV     1222   /* 书签：上一个 */
#define IDM_EXPLORER    1223   /* 打开所在文件夹（右键菜单） */

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

/* UI language（帮助 → 界面语言） */
#define IDM_UI_LANG_FIRST  1650
#define IDM_UI_LANG_ZH     1650
#define IDM_UI_LANG_EN     1651
#define IDM_UI_LANG_LAST   1651

/* ----- Dialogs ----- */
#define IDD_FIND    2100
#define IDD_REPLACE 2101
#define IDD_GOTO    2102
#define IDD_ABOUT   2103
#define IDD_OPENENC 2104
#define IDD_PROGRESS 2105   /* 大文件加载/保存进度框 */

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
#define IDC_FIND_REGEX  2213    /* 正则表达式复选框 */
#define IDC_FIND_LBL_TEXT 2220   /* "查找内容:" 静态标签（运行时覆盖文字） */
#define IDC_REPL_LBL_TEXT 2221   /* "替换为:" */

/* Goto */
#define IDC_GOTO_TEXT   2301
#define IDC_GOTO_OK     2302
#define IDC_GOTO_CANCEL 2303
#define IDC_GOTO_LABEL  2304

/* About */
#define IDC_ABOUT_LINE1 2401     /* 四行说明文字（运行时覆盖） */
#define IDC_ABOUT_LINE2 2402
#define IDC_ABOUT_LINE3 2403
#define IDC_ABOUT_LINE4 2404

/* Open encoding */
#define IDC_OPENENC_LIST 2501
#define IDC_OPENENC_LBL  2502    /* "选择编码:" */

/* Load/save progress */
#define IDC_PROG_TEXT   2601
#define IDC_PROG_BAR    2602
#define IDC_PROG_CANCEL 2603

#endif /* RESOURCE_H */
