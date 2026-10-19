#ifndef DWTEXT_H
#define DWTEXT_H
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DwtFmt DwtFmt;

BOOL    Dwt_Ok(void);
DwtFmt* Dwt_Font(const wchar_t* face, int px, int weight, BOOL italic,
                 int lineH, int asc);
void    Dwt_FontsChanged(void);
int     Dwt_Width(DwtFmt* f, const wchar_t* s, int len);
void    Dwt_Text(DwtFmt* f, int x, int baseY, const wchar_t* s, int len,
                 int w, COLORREF c);
void    Dwt_Line(int x, int y, int w, COLORREF c);
void    Dwt_Flush(HDC hdc, const RECT* rc);

#ifdef __cplusplus
}
#endif

#endif
