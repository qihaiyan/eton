/* stress.c — mermaid 解析/布局/释放 压力探针：定位堆损坏。
   用法：stress [--chk] file1.mmd [file2.mmd ...]（每文件 3000 次解析-测量-释放）
     --chk：CRT 调试堆全量校验（需把构建中 /MT /O2 换成 /MTd /Od /Zi 才生效）
   构建（x64 Native Tools 下，在仓库根）：
     cl /nologo /W3 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
        /Isrc /Ideps\scintilla /Ideps\lexilla /Ideps\md4c ^
        /Febuild\stress.exe stress.c src\mermaid.c ^
        /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib kernel32.lib */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>
#include "common.h"

static int s_zoom = 175;
int UI_Scale(int px) { return MulDiv(px, s_zoom, 100); }

static HFONT Mk(int px, int weight, BOOL it, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: stress [--chk] in.mmd ...\n"); return 1; }
    BOOL chk = argc > 1 && strcmp(argv[1], "--chk") == 0;
    if (chk) {   /* --chk：CRT 调试堆全量校验（需 /MTd 构建方有效） */
        argv++; argc--;
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF);
    }
    typedef LONG(WINAPI* SPDAC_t)(LONG);
    SPDAC_t fn = (SPDAC_t)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                         "SetProcessDpiAwarenessContext");
    if (fn) fn(-4);

    MdFonts f;
    ZeroMemory(&f, sizeof(f));
    int px = MulDiv(11, s_zoom * 96 / 100, 72);
    f.body = Mk(px, FW_NORMAL, FALSE, L"Segoe UI");
    f.bold = Mk(px, FW_BOLD, FALSE, L"Segoe UI");
    f.sm = Mk(MulDiv(9, s_zoom * 96 / 100, 72), FW_NORMAL, FALSE, L"Segoe UI");
    f.mono = Mk(MulDiv(10, s_zoom * 96 / 100, 72), FW_NORMAL, FALSE, L"Consolas");
    f.mathIt = Mk(px, FW_NORMAL, TRUE, L"Cambria Math");
    f.mathUp = Mk(px, FW_NORMAL, FALSE, L"Cambria Math");
    f.mathItS = Mk((int)(px * 0.72 + 0.5), FW_NORMAL, TRUE, L"Cambria Math");
    f.mathUpS = Mk((int)(px * 0.72 + 0.5), FW_NORMAL, FALSE, L"Cambria Math");
    f.mathItSS = Mk((int)(px * 0.55 + 0.5), FW_NORMAL, TRUE, L"Cambria Math");
    f.mathUpSS = Mk((int)(px * 0.55 + 0.5), FW_NORMAL, FALSE, L"Cambria Math");

    HDC scr = GetDC(NULL);
    HDC hdc = CreateCompatibleDC(scr);
    HBITMAP bmp = CreateCompatibleBitmap(scr, 800, 600);
    HGDIOBJ ob = SelectObject(hdc, bmp);

    for (int ai = 1; ai < argc; ai++) {
        FILE* fp = fopen(argv[ai], "rb");
        if (!fp) { printf("skip %s\n", argv[ai]); continue; }
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char* buf = (char*)malloc(n + 1);
        fread(buf, 1, n, fp);
        buf[n] = 0;
        fclose(fp);

        printf("stress %-28s ", argv[ai]);
        fflush(stdout);
        for (int it = 0; it < 3000; it++) {
            MermaidDiagram* d = Mermaid_Parse(buf, (int)n);
            if (d) {
                Mermaid_Measure(d, hdc, &f);
                Mermaid_Free(d);
            }

            if (it % 500 == 499) { printf("."); fflush(stdout); }
        }
        printf(" OK\n");
        fflush(stdout);
        free(buf);
    }
    printf("ALL DONE\n");
    return 0;
}
