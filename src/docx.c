/*
 * docx.c — Markdown → Word (.docx) 导出
 *
 * 纯 OOXML 生成，零外部依赖：
 *  - store-only ZIP 打包（CRC32 + 本地头 + 中央目录）
 *  - document.xml / styles.xml / numbering.xml / rels / [Content_Types].xml
 *  - 数学公式生成 OMML（m:oMath），Word 内可直接编辑
 *  - Mermaid 图用原生 GDI 绘制渲染为矢量 EMF 嵌入
 *  - 本地/已缓存图片按原始字节嵌入（png/jpeg/gif/bmp/tiff 魔数嗅探）
 *
 * 设置环境变量 ETON_EXPORT_PATH 可跳过保存对话框直接写指定路径
 * （供自动化/脚本使用）。
 */

#include "common.h"
#include <shlwapi.h>

/* ---------------- 动态 UTF-8 缓冲 ---------------- */

typedef struct { char* p; size_t len, cap; } XB;

static void XbReserve(XB* b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->len + extra + 1) nc *= 2;
    char* q = (char*)realloc(b->p, nc);
    if (!q) return;
    b->p = q;
    b->cap = nc;
}

static void XbPut(XB* b, const char* s, size_t n) {
    XbReserve(b, n);
    if (!b->p) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void Xs(XB* b, const char* s) { XbPut(b, s, strlen(s)); }

static void Xnum(XB* b, long long v) {
    char t[32];
    sprintf_s(t, sizeof(t), "%lld", v);
    Xs(b, t);
}

/* 转义并追加宽字符串（XML 文本/属性通用） */
static void XwSeg(XB* b, const wchar_t* w, int n) {
    if (!w) return;
    char utf8[16];
    for (int i = 0; i < n && w[i]; i++) {
        wchar_t ch = w[i];
        const char* ent = NULL;
        switch (ch) {
            case L'&':  ent = "&amp;";  break;
            case L'<':  ent = "&lt;";   break;
            case L'>':  ent = "&gt;";   break;
            case L'"':  ent = "&quot;"; break;
            case L'\'': ent = "&apos;"; break;
        }
        if (ent) { Xs(b, ent); continue; }
        if (ch < 0x20 && ch != L'	') continue;   /* XML 非法控制字符直接丢弃 */
        int m = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, sizeof(utf8), NULL, NULL);
        if (m > 0) XbPut(b, utf8, (size_t)m);
    }
}

static void Xw(XB* b, const wchar_t* w) {
    XwSeg(b, w, w ? (int)wcslen(w) : 0);
}

/* ---------------- CRC32 + store-only ZIP ---------------- */

static DWORD s_crcTbl[256];
static BOOL s_crcOk = FALSE;

static void CrcInit(void) {
    for (DWORD i = 0; i < 256; i++) {
        DWORD c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crcTbl[i] = c;
    }
    s_crcOk = TRUE;
}

static DWORD Crc32(const void* data, size_t n) {
    if (!s_crcOk) CrcInit();
    DWORD c = 0xFFFFFFFFu;
    const BYTE* p = (const BYTE*)data;
    for (size_t i = 0; i < n; i++)
        c = s_crcTbl[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

typedef struct {
    char  name[80];
    BYTE* data;
    DWORD size;
    DWORD crc;
    DWORD off;      /* 本地头在文件中的偏移 */
} ZipEnt;

typedef struct { ZipEnt* e; int n, cap; } Zip;

static BOOL ZipAdd(Zip* z, const char* name, const void* data, DWORD size) {
    if (z->n >= z->cap) {
        int nc = z->cap ? z->cap * 2 : 16;
        ZipEnt* q = (ZipEnt*)realloc(z->e, (size_t)nc * sizeof(ZipEnt));
        if (!q) return FALSE;
        z->e = q;
        z->cap = nc;
    }
    ZipEnt* t = &z->e[z->n];
    ZeroMemory(t, sizeof(*t));
    strncpy_s(t->name, sizeof(t->name), name, _TRUNCATE);
    t->data = (BYTE*)malloc(size ? size : 1);
    if (!t->data) return FALSE;
    if (size) memcpy(t->data, data, size);
    t->size = size;
    t->crc = Crc32(t->data, size);
    z->n++;
    return TRUE;
}

static void ZipFree(Zip* z) {
    for (int i = 0; i < z->n; i++) free(z->e[i].data);
    free(z->e);
    z->e = NULL; z->n = z->cap = 0;
}

static void ZipW16(XB* o, unsigned v) {
    char t[2] = { (char)(v & 0xFF), (char)((v >> 8) & 0xFF) };
    XbPut(o, t, 2);
}
static void ZipW32(XB* o, unsigned long v) {
    char t[4];
    t[0] = (char)(v & 0xFF); t[1] = (char)((v >> 8) & 0xFF);
    t[2] = (char)((v >> 16) & 0xFF); t[3] = (char)((v >> 24) & 0xFF);
    XbPut(o, t, 4);
}

/* 写出 zip（所有条目 store 不压缩，文件名按 UTF-8 标志位） */
static BOOL ZipWriteFile(Zip* z, const wchar_t* path) {
    XB o = { 0 };
    for (int i = 0; i < z->n; i++) {
        ZipEnt* e = &z->e[i];
        e->off = (DWORD)o.len;
        ZipW32(&o, 0x04034b50);          /* local file header */
        ZipW16(&o, 20);                  /* version needed */
        ZipW16(&o, 0x0800);              /* UTF-8 名称 */
        ZipW16(&o, 0);                   /* method: store */
        ZipW16(&o, 0); ZipW16(&o, 0x21); /* time 00:00, date 1980-01-01 */
        ZipW32(&o, e->crc);
        ZipW32(&o, e->size);
        ZipW32(&o, e->size);
        ZipW16(&o, (unsigned)strlen(e->name));
        ZipW16(&o, 0);                    /* extra len（本地头无 comment 字段） */
        Xs(&o, e->name);
        XbPut(&o, (const char*)e->data, e->size);
    }
    DWORD cdStart = (DWORD)o.len;
    for (int i = 0; i < z->n; i++) {
        const ZipEnt* e = &z->e[i];
        ZipW32(&o, 0x02014b50);          /* central directory */
        ZipW16(&o, 20); ZipW16(&o, 20);
        ZipW16(&o, 0x0800); ZipW16(&o, 0);
        ZipW16(&o, 0); ZipW16(&o, 0x21); /* time 00:00, date 1980-01-01 */
        ZipW32(&o, e->crc);
        ZipW32(&o, e->size);
        ZipW32(&o, e->size);
        ZipW16(&o, (unsigned)strlen(e->name));
        ZipW16(&o, 0); ZipW16(&o, 0); ZipW16(&o, 0); ZipW16(&o, 0);
        ZipW32(&o, 0); ZipW32(&o, e->off);
        Xs(&o, e->name);
    }
    DWORD cdSize = (DWORD)o.len - cdStart;
    ZipW32(&o, 0x06054b50);              /* EOCD */
    ZipW16(&o, 0); ZipW16(&o, 0);
    ZipW16(&o, (unsigned)z->n); ZipW16(&o, (unsigned)z->n);
    ZipW32(&o, cdSize);
    ZipW32(&o, cdStart);
    ZipW16(&o, 0);

    BOOL ok = FALSE;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        ok = o.p && WriteFile(f, o.p, (DWORD)o.len, &wr, NULL) && wr == (DWORD)o.len;
        CloseHandle(f);
    }
    free(o.p);
    return ok;
}

/* ---------------- 导出上下文 ---------------- */

typedef struct {
    XB doc;             /* document.xml body */
    XB numAbs;          /* numbering.xml 按需生成的有序列表定义 */
    XB rels;            /* document.xml.rels 的图片/超链接项 */
    Zip zip;
    int nextRid;        /* 下一个 rId 编号（1=styles 2=numbering 已占） */
    int nextMedia;      /* word/media/imageN */
    int drawId;         /* docPr id */
    int nextNumId;      /* 有序列表实例编号 */
    const MdFonts* fonts;
    struct { wchar_t url[2048]; int rid; } links[256];
    int nLinks;
} DCX;

/* 超链接：同 URL 去重，返回 rId */
static int AddHyperlink(DCX* c, const wchar_t* url) {
    for (int i = 0; i < c->nLinks; i++)
        if (wcscmp(c->links[i].url, url) == 0) return c->links[i].rid;
    if (c->nLinks >= 256) return 0;
    int rid = c->nextRid++;
    wcscpy_s(c->links[c->nLinks].url, 2048, url);
    c->links[c->nLinks].rid = rid;
    c->nLinks++;
    Xs(&c->rels, "<Relationship Id=\"rId");
    Xnum(&c->rels, rid);
    Xs(&c->rels, "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" TargetMode=\"External\" Target=\"");
    Xw(&c->rels, url);
    Xs(&c->rels, "\"/>");
    return rid;
}

/* ---------------- 图片魔数嗅探 ---------------- */

static unsigned Be16(const BYTE* p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned Be32(const BYTE* p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | p[3];
}
static unsigned Le16(const BYTE* p) { return ((unsigned)p[1] << 8) | p[0]; }
static unsigned Le32(const BYTE* p) {
    return ((unsigned)p[3] << 24) | ((unsigned)p[2] << 16) |
           ((unsigned)p[1] << 8) | p[0];
}

static const char* SniffImage(const BYTE* d, DWORD n, int* w, int* h) {
    *w = 0; *h = 0;
    if (n >= 24 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') {
        *w = (int)Be32(d + 16);
        *h = (int)Be32(d + 20);
        return "png";
    }
    if (n >= 10 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') {
        *w = (int)Le16(d + 6);
        *h = (int)Le16(d + 8);
        return "gif";
    }
    if (n >= 26 && d[0] == 'B' && d[1] == 'M') {
        *w = (int)Le32(d + 18);
        int hh = (int)Le32(d + 22);
        *h = hh < 0 ? -hh : hh;
        return "bmp";
    }
    if (n >= 8 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) {
        DWORD i = 2;
        while (i + 9 < n) {
            if (d[i] != 0xFF) break;
            BYTE m = d[i + 1];
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                *h = (int)Be16(d + i + 5);
                *w = (int)Be16(d + i + 7);
                break;
            }
            DWORD seg = Be16(d + i + 2);
            if (seg < 2) break;
            i += 2 + seg;
        }
        return "jpeg";
    }
    if (n >= 8 && ((d[0] == 'I' && d[1] == 'I' && d[2] == 0x2A) ||
                   (d[0] == 'M' && d[1] == 'M' && d[3] == 0x2A)))
        return "tiff";
    return NULL;
}

/* 嵌入媒体字节，返回 rId；emuW/emuH 返回建议尺寸（96dpi 假设，宽超页缩放） */
static int AddMediaBytes(DCX* c, const void* bytes, DWORD size, const char* ext,
                         int pxW, int pxH, long long* emuW, long long* emuH) {
    char name[64];
    int idx = ++c->nextMedia;
    sprintf_s(name, sizeof(name), "word/media/image%d.%s", idx, ext);
    if (!ZipAdd(&c->zip, name, bytes, size)) return 0;
    int rid = c->nextRid++;
    char rel[160];
    sprintf_s(rel, sizeof(rel),
              "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/"
              "officeDocument/2006/relationships/image\" Target=\"media/image%d.%s\"/>",
              rid, idx, ext);
    Xs(&c->rels, rel);
    if (pxW <= 0) pxW = 400;
    if (pxH <= 0) pxH = 300;
    if (pxW > 550) { pxH = pxH * 550 / pxW; pxW = 550; }
    *emuW = (long long)pxW * 9525;
    *emuH = (long long)pxH * 9525;
    return rid;
}

static void DrawingRun(DCX* c, int rid, long long emuW, long long emuH) {
    int id = ++c->drawId;
    XB* b = &c->doc;
    Xs(b, "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">");
    Xs(b, "<wp:extent cx=\""); Xnum(b, emuW);
    Xs(b, "\" cy=\"");         Xnum(b, emuH);
    Xs(b, "\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>");
    Xs(b, "<wp:docPr id=\"");  Xnum(b, id);
    Xs(b, "\" name=\"Picture "); Xnum(b, id);
    Xs(b, "\"/><a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">");
    Xs(b, "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">");
    Xs(b, "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">");
    Xs(b, "<pic:nvPicPr><pic:cNvPr id=\""); Xnum(b, id);
    Xs(b, "\" name=\"image");  Xnum(b, id);
    Xs(b, "\"/><pic:cNvPicPr/></pic:nvPicPr>");
    Xs(b, "<pic:blipFill><a:blip r:embed=\"rId"); Xnum(b, rid);
    Xs(b, "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>");
    Xs(b, "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\""); Xnum(b, emuW);
    Xs(b, "\" cy=\"");         Xnum(b, emuH);
    Xs(b, "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>");
    Xs(b, "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>");
}

/* 嵌入磁盘图片文件（本地/缓存），返回 rId，0=失败 */
static int AddImageFile(DCX* c, const wchar_t* path, long long* emuW, long long* emuH) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(f);
        return 0;
    }
    BYTE* buf = (BYTE*)malloc((size_t)sz.QuadPart);
    if (!buf) { CloseHandle(f); return 0; }
    DWORD rd = 0, total = (DWORD)sz.QuadPart;
    BOOL ok = ReadFile(f, buf, total, &rd, NULL) && rd == total;
    CloseHandle(f);
    if (!ok) { free(buf); return 0; }
    int w = 0, h = 0;
    const char* ext = SniffImage(buf, total, &w, &h);
    if (!ext) { free(buf); return 0; }
    if (w <= 0 || h <= 0) {   /* 尺寸未知时经 GDI+ 拿 */
        UINT gw = 0, gh = 0;
        if (MdView_ImageSize(path, &gw, &gh)) { w = (int)gw; h = (int)gh; }
    }
    int rid = AddMediaBytes(c, buf, total, ext, w, h, emuW, emuH);
    free(buf);
    return rid;
}

/* ---------------- OMML 公式 ---------------- */

static void OmmlNode(XB* b, const MathBox* m);

static const MathBox* Kid(const MathBox* m, int i) {
    return (m && m->nKids > i) ? m->kids[i] : NULL;
}

/* 行内容：BIGOP + 空底 SCRIPT 合并为 nary / limLow */
static void OmmlKids(XB* b, const MathBox* row) {
    for (int i = 0; i < row->nKids; i++) {
        const MathBox* k = row->kids[i];
        if (!k) continue;
        if (k->kind == MB_BIGOP && i + 1 < row->nKids) {
            const MathBox* nx = row->kids[i + 1];
            const wchar_t* g = Kid(k, 0) && Kid(k, 0)->text ? Kid(k, 0)->text : L"";
            BOOL multi = wcslen(g) > 1;
            if (nx && nx->kind == MB_SCRIPT && nx->nKids > 0 && !nx->kids[0]) {
                const MathBox* sub = Kid(nx, 2);
                const MathBox* sup = Kid(nx, 1);
                if (!multi) {       /* ∑ ∫ ∏ … → 上下限运算符 */
                    Xs(b, "<m:nary><m:naryPr><m:chr m:val=\"");
                    Xw(b, g);
                    Xs(b, "\"/><m:limLoc m:val=\"undOvr\"/>");
                    if (!sup) Xs(b, "<m:supHide m:val=\"1\"/>");
                    if (!sub) Xs(b, "<m:subHide m:val=\"1\"/>");
                    Xs(b, "</m:naryPr>");
                    if (sub) { Xs(b, "<m:sub>"); OmmlNode(b, sub); Xs(b, "</m:sub>"); }
                    if (sup) { Xs(b, "<m:sup>"); OmmlNode(b, sup); Xs(b, "</m:sup>"); }
                    Xs(b, "<m:e/></m:nary>");
                    i++;
                    continue;
                }
                if (sub && wcsncmp(g, L"lim", 3) == 0) {   /* lim_{…} */
                    Xs(b, "<m:limLow><m:e>");
                    OmmlNode(b, k);
                    Xs(b, "</m:e><m:lim>");
                    OmmlNode(b, sub);
                    Xs(b, "</m:lim></m:limLow>");
                    i++;
                    continue;
                }
            }
        }
        OmmlNode(b, k);
    }
}

static void OmmlNode(XB* b, const MathBox* m) {
    if (!m) return;
    switch (m->kind) {
        case MB_ROW:
            OmmlKids(b, m);
            break;
        case MB_GLYPHS:
            if (!m->text || !m->text[0]) break;
            Xs(b, "<m:r>");
            if (!m->italic) Xs(b, "<m:rPr><m:sty m:val=\"p\"/></m:rPr>");
            Xs(b, "<m:t xml:space=\"preserve\">");
            Xw(b, m->text);
            Xs(b, "</m:t></m:r>");
            break;
        case MB_FRAC:
            Xs(b, "<m:f><m:num>");
            OmmlNode(b, Kid(m, 0));
            Xs(b, "</m:num><m:den>");
            OmmlNode(b, Kid(m, 1));
            Xs(b, "</m:den></m:f>");
            break;
        case MB_RADICAL:
            Xs(b, "<m:rad><m:radPr><m:degHide m:val=\"1\"/></m:radPr>"
                  "<m:deg/><m:e>");
            OmmlNode(b, Kid(m, 0));
            Xs(b, "</m:e></m:rad>");
            break;
        case MB_SCRIPT: {
            const MathBox* base = Kid(m, 0);
            const MathBox* sup  = Kid(m, 1);
            const MathBox* sub  = Kid(m, 2);
            if (sup && sub) {
                Xs(b, "<m:sSubSup><m:e>");
                OmmlNode(b, base);
                Xs(b, "</m:e><m:sub>");
                OmmlNode(b, sub);
                Xs(b, "</m:sub><m:sup>");
                OmmlNode(b, sup);
                Xs(b, "</m:sup></m:sSubSup>");
            } else if (sup) {
                Xs(b, "<m:sSup><m:e>");
                OmmlNode(b, base);
                Xs(b, "</m:e><m:sup>");
                OmmlNode(b, sup);
                Xs(b, "</m:sup></m:sSup>");
            } else if (sub) {
                Xs(b, "<m:sSub><m:e>");
                OmmlNode(b, base);
                Xs(b, "</m:e><m:sub>");
                OmmlNode(b, sub);
                Xs(b, "</m:sub></m:sSub>");
            } else {
                OmmlNode(b, base);
            }
            break;
        }
        case MB_BIGOP:   /* 未与后续脚本合并：按普通符号输出 */
            OmmlNode(b, Kid(m, 0));
            break;
        case MB_ENV: {
            /* 环境矩阵：m:d（分隔符）包裹 m:m（矩阵行/列） */
            static const char* kBeg[7] = { NULL, "(", "[", "{", "|", "\xE2\x80\x96", "{" };
            static const char* kEnd[7] = { NULL, ")", "]", "}", "|", "\xE2\x80\x96", NULL };
            int ev = m->env & 7;
            BOOL hasDelim = (ev != MBENV_PLAIN);
            if (hasDelim) {
                Xs(b, "<m:d><m:dPr>");
                if (kBeg[ev]) { Xs(b, "<m:begChr m:val=\""); Xs(b, kBeg[ev]); Xs(b, "\"/>"); }
                else Xs(b, "<m:begChr m:val=\"\"/>");
                if (kEnd[ev]) { Xs(b, "<m:endChr m:val=\""); Xs(b, kEnd[ev]); Xs(b, "\"/>"); }
                else Xs(b, "<m:endChr m:val=\"\"/>");
                Xs(b, "</m:dPr><m:e>");
            }
            int nCols = m->cols > 0 ? m->cols : 1;
            Xs(b, "<m:m>");
            for (int i = 0; i < m->nKids; i++) {
                const MathBox* r = m->kids[i];
                Xs(b, "<m:mr>");
                for (int j = 0; j < nCols; j++) {
                    Xs(b, "<m:e>");
                    if (r && j < r->nKids) OmmlNode(b, r->kids[j]);
                    Xs(b, "</m:e>");
                }
                Xs(b, "</m:mr>");
            }
            Xs(b, "</m:m>");
            if (hasDelim) Xs(b, "</m:e></m:d>");
            break;
        }
    }
}

static void OmmlMath(XB* b, const MathBox* m, BOOL display) {
    if (display) Xs(b, "<m:oMathPara>");
    Xs(b, "<m:oMath>");
    OmmlNode(b, m);
    Xs(b, "</m:oMath>");
    if (display) Xs(b, "</m:oMathPara>");
}

/* ---------------- Mermaid → EMF ---------------- */

static int AddDiagram(DCX* c, MermaidDiagram* d, long long* emuW, long long* emuH) {
    HDC ref = GetDC(NULL);
    if (!ref || !c->fonts) { if (ref) ReleaseDC(NULL, ref); return 0; }
    int rid = 0;
    SIZE sz = Mermaid_Measure(d, ref, c->fonts);
    if (sz.cx > 0 && sz.cy > 0) {
        RECT rc = { 0, 0, sz.cx, sz.cy };
        HDC mdc = CreateEnhMetaFileW(ref, NULL, &rc, L"eton docx\0diagram\0");
        if (mdc) {
            BOOL saveDark = g_dark;
            g_dark = FALSE;                     /* Word 文档固定亮色 */
            MdTheme th;
            MdTheme_Build(&th);
            g_dark = saveDark;
            SetBkMode(mdc, TRANSPARENT);
            SetGraphicsMode(mdc, GM_ADVANCED);
            Mermaid_Draw(d, mdc, 0, 0, c->fonts, &th);
            HENHMETAFILE he = CloseEnhMetaFile(mdc);
            if (he) {
                UINT need = GetEnhMetaFileBits(he, 0, NULL);
                if (need > 0) {
                    BYTE* buf = (BYTE*)malloc(need);
                    if (buf && GetEnhMetaFileBits(he, need, buf)) {
                        int dpi = GetDeviceCaps(ref, LOGPIXELSX);
                        if (dpi <= 0) dpi = 96;
                        long long w = (long long)sz.cx * 914400 / dpi;
                        long long h = (long long)sz.cy * 914400 / dpi;
                        long long maxW = 550LL * 9525;
                        if (w > maxW) { h = h * maxW / w; w = maxW; }
                        rid = AddMediaBytes(c, buf, need, "emf", 0, 0, emuW, emuH);
                        if (rid) { *emuW = w; *emuH = h; }
                    }
                    free(buf);
                }
                DeleteEnhMetaFile(he);
            }
        }
    }
    ReleaseDC(NULL, ref);
    return rid;
}

/* ---------------- 文档内容生成 ---------------- */

static void WalkBlock(DCX* c, const MdBlock* b, int lvl, int qd);

/* 段落运行输出：粗斜/删除线/行内代码/链接/公式/行内图片/软换行 */
static void RunProps(DCX* c, unsigned sty, int linkRid) {
    XB* b = &c->doc;
    BOOL any = (sty & (STY_BOLD | STY_EM | STY_STRIKE | STY_CODE | STY_LINK)) ||
               linkRid;
    if (!any) return;
    Xs(b, "<w:rPr>");
    if (sty & STY_CODE)   Xs(b, "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/>");
    if (sty & STY_BOLD)   Xs(b, "<w:b/>");
    if (sty & STY_EM)     Xs(b, "<w:i/>");
    if (sty & STY_STRIKE) Xs(b, "<w:strike/>");
    if (linkRid)          Xs(b, "<w:color w:val=\"0563C1\"/><w:u w:val=\"single\"/>");
    if (sty & STY_CODE)   Xs(b, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F0F0F0\"/>");
    Xs(b, "</w:rPr>");
}

static void TextRun(DCX* c, const wchar_t* text, unsigned sty, int linkRid) {
    if (!text || !text[0]) return;
    XB* b = &c->doc;
    if (linkRid) {
        Xs(b, "<w:hyperlink r:id=\"rId");
        Xnum(b, linkRid);
        Xs(b, "\" w:history=\"1\">");
    }
    Xs(b, "<w:r>");
    RunProps(c, sty, linkRid);
    Xs(b, "<w:t xml:space=\"preserve\">");
    Xw(b, text);
    Xs(b, "</w:t></w:r>");
    if (linkRid) Xs(b, "</w:hyperlink>");
}

/* 段落内运行序列 */
static void ParaRuns(DCX* c, const MdRun* runs, int nRuns, int qd) {
    (void)qd;
    XB* b = &c->doc;
    for (int i = 0; i < nRuns; i++) {
        const MdRun* r = &runs[i];
        if (r->style & STY_BR) { Xs(b, "<w:r><w:br/></w:r>"); continue; }
        if (r->style & STY_MATH) {
            MathBox* m = r->math;
            if (!m && r->text) m = Math_Build(r->text);
            if (m) {
                OmmlMath(b, m, (r->style & STY_MATHDISP) && nRuns == 1);
                if (!r->math) Math_Free(m);
            }
            continue;
        }
        if (r->style & STY_IMG) {
            wchar_t full[MAX_PATH];
            if (MdView_ResolveImage(r->href ? r->href : L"", full, MAX_PATH)) {
                long long ew = 0, eh = 0;
                int rid = AddImageFile(c, full, &ew, &eh);
                if (rid) DrawingRun(c, rid, ew, eh);
            }
            if (r->text && r->text[0]) TextRun(c, r->text, 0, 0);
            continue;
        }
        if (!r->text || !r->text[0]) continue;
        int rid = 0;
        const wchar_t* href = r->link ? r->link : r->href;
        if ((r->style & STY_LINK) && href && href[0] && href[0] != L'#')
            rid = AddHyperlink(c, href);
        TextRun(c, r->text, r->style, rid);
    }
}

/* 判断段落是否为“图片段落”（只有一个图片运行，其余是空白） */
static const MdRun* SoleImageRun(const MdBlock* b) {
    const MdRun* img = NULL;
    for (int i = 0; i < b->nRuns; i++) {
        const MdRun* r = &b->runs[i];
        if (r->style & STY_BR) continue;
        if (!r->text) continue;
        for (const wchar_t* p = r->text; *p; p++) {
            if (*p != L' ' && *p != L'\t') {
                if (r->style & STY_IMG) {
                    if (img) return NULL;
                    img = r;
                    break;
                }
                return NULL;
            }
        }
    }
    return img;
}

/* 判断段落是否为“独行公式” */
static const MdRun* SoleDisplayMathRun(const MdBlock* b) {
    int nSig = 0;
    const MdRun* math = NULL;
    for (int i = 0; i < b->nRuns; i++) {
        const MdRun* r = &b->runs[i];
        if (r->style & STY_BR) continue;
        if (r->style & STY_MATH) {
            if (!(r->style & STY_MATHDISP)) return NULL;
            math = r;
            nSig++;
            continue;
        }
        if (!r->text) continue;
        for (const wchar_t* p = r->text; *p; p++)
            if (*p != L' ' && *p != L'\t') return NULL;
    }
    return (nSig == 1) ? math : NULL;
}

/* 列表项编号定义（按需生成，numId 起自 100；每个有序列表独立实例以重新计数） */
static int NewOrderNum(DCX* c) {
    int id = 100 + c->nextNumId++;
    XB* b = &c->numAbs;
    char head[96];
    sprintf_s(head, sizeof(head), "<w:abstractNum w:abstractNumId=\"%d\">", id);
    Xs(b, head);
    static const char* lvlFmt[3] = { "%1.", "%1.%2.", "%1.%2.%3." };
    static const int  lvlInd[3]  = { 720, 1440, 2160 };
    for (int l = 0; l < 3; l++) {
        char t[320];
        sprintf_s(t, sizeof(t),
            "<w:lvl w:ilvl=\"%d\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/>"
            "<w:pStyle w:val=\"ListParagraph\"/><w:lvlText w:val=\"%s\"/>"
            "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"%d\" w:hanging=\"360\"/></w:pPr></w:lvl>",
            l, lvlFmt[l], lvlInd[l]);
        Xs(b, t);
    }
    Xs(b, "</w:abstractNum>");
    return id;
}

static void WalkList(DCX* c, const MdBlock* list, int lvl, int qd);

static void WalkLi(DCX* c, const MdBlock* li, int lvl, int qd, int numId) {
    BOOL firstPara = TRUE;
    for (int i = 0; i < li->nChildren; i++) {
        const MdBlock* ch = li->children[i];
        if (!ch) continue;
        if (ch->type == MDB_UL || ch->type == MDB_OL) {
            WalkList(c, ch, lvl + 1, qd);
            continue;
        }
        BOOL taskMark = li->isTask && firstPara;
        BOOL numbered = firstPara && !li->isTask && numId;
        firstPara = FALSE;
        XB* b = &c->doc;
        if (ch->type == MDB_P || ch->type == MDB_H) {
            Xs(b, "<w:p><w:pPr>");
            if (numbered) {
                Xs(b, "<w:numPr><w:ilvl w:val=\""); Xnum(b, lvl);
                Xs(b, "\"/><w:numId w:val=\"");     Xnum(b, numId);
                Xs(b, "\"/></w:numPr>");
            } else {
                int ind = 720 * (lvl + 1);
                Xs(b, "<w:ind w:left=\""); Xnum(b, ind); Xs(b, "\"/>");
            }
            if (qd > 0) {
                Xs(b, "<w:pBdr><w:left w:val=\"single\" w:sz=\"12\" w:space=\"4\" "
                      "w:color=\"BFBFBF\"/></w:pBdr>");
            }
            Xs(b, "</w:pPr>");
            if (taskMark) {
                wchar_t mark[2] = { (li->taskMark == L' ' || !li->taskMark) ? 0x2610 : 0x2611, 0 };
                TextRun(c, mark, 0, 0);
                TextRun(c, L" ", 0, 0);
            }
            ParaRuns(c, ch->runs, ch->nRuns, qd);
            Xs(b, "</w:p>");
        } else {
            WalkBlock(c, ch, lvl + 1, qd);
        }
    }
}

static void WalkList(DCX* c, const MdBlock* list, int lvl, int qd) {
    int numId = (list->type == MDB_OL) ? NewOrderNum(c) : 1;
    for (int i = 0; i < list->nChildren; i++) {
        const MdBlock* li = list->children[i];
        if (li && li->type == MDB_LI) WalkLi(c, li, lvl, qd, numId);
    }
}

static void WalkTable(DCX* c, const MdBlock* b, int qd) {
    XB* x = &c->doc;
    int nC = b->nCols, nR = b->nRows;
    if (nC <= 0 || nR <= 0 || !b->cells) return;
    Xs(x, "<w:tbl><w:tblPr><w:tblW w:w=\"5000\" w:type=\"pct\"/>"
          "<w:tblBorders>"
          "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
          "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
          "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
          "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
          "<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
          "<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
          "</w:tblBorders></w:tblPr><w:tblGrid>");
    for (int i = 0; i < nC; i++) Xs(x, "<w:gridCol/>");
    Xs(x, "</w:tblGrid>");
    for (int r = 0; r < nR; r++) {
        Xs(x, "<w:tr>");
        for (int cc = 0; cc < nC; cc++) {
            Xs(x, "<w:tc><w:tcPr>");
            if (r == 0) Xs(x, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"EDEDED\"/>");
            Xs(x, "<w:vAlign w:val=\"center\"/></w:tcPr><w:p><w:pPr>");
            int al = b->aligns ? b->aligns[cc] : 0;
            if (al == 1)      Xs(x, "<w:jc w:val=\"center\"/>");
            else if (al == 2) Xs(x, "<w:jc w:val=\"right\"/>");
            Xs(x, "</w:pPr>");
            const MdBlock* cell = b->cells[r * nC + cc];
            if (cell) {
                unsigned sty = (r == 0) ? STY_BOLD : 0;
                for (int k = 0; k < cell->nRuns; k++) {
                    const MdRun* run = &cell->runs[k];
                    if (run->style & STY_MATH)
                        ParaRuns(c, cell->runs + k, 1, qd);
                    else
                        TextRun(c, run->text, run->style | sty, 0);
                }
            }
            Xs(x, "</w:p></w:tc>");
        }
        Xs(x, "</w:tr>");
    }
    Xs(x, "</w:tbl><w:p/>");
}

/* 普通文本段落（引用深度 qd 决定左边框+缩进） */
static void EmitPara(DCX* c, const MdBlock* b, int qd) {
    XB* x = &c->doc;
    Xs(x, "<w:p><w:pPr>");
    if (qd > 0) {
        Xs(x, "<w:pBdr><w:left w:val=\"single\" w:sz=\"12\" w:space=\"4\" "
              "w:color=\"BFBFBF\"/></w:pBdr>");
        Xs(x, "<w:ind w:left=\""); Xnum(x, 367 * qd); Xs(x, "\"/>");
    }
    Xs(x, "</w:pPr>");
    ParaRuns(c, b->runs, b->nRuns, qd);
    Xs(x, "</w:p>");
}

static BOOL EmitImagePara(DCX* c, const MdRun* img) {
    wchar_t full[MAX_PATH];
    if (!MdView_ResolveImage(img->href ? img->href : L"", full, MAX_PATH)) return FALSE;
    long long ew = 0, eh = 0;
    int rid = AddImageFile(c, full, &ew, &eh);
    if (!rid) return FALSE;
    XB* x = &c->doc;
    Xs(x, "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>");
    DrawingRun(c, rid, ew, eh);
    Xs(x, "</w:p>");
    return TRUE;
}

static BOOL EmitMathPara(DCX* c, const MdRun* r) {
    MathBox* m = r->math;
    if (!m && r->text) m = Math_Build(r->text);
    if (!m) return FALSE;
    XB* x = &c->doc;
    Xs(x, "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>");
    OmmlMath(x, m, TRUE);
    if (!r->math) Math_Free(m);
    Xs(x, "</w:p>");
    return TRUE;
}

static void WalkBlock(DCX* c, const MdBlock* b, int lvl, int qd) {
    if (!b) return;
    XB* x = &c->doc;
    switch (b->type) {
        case MDB_P: {
            const MdRun* img = SoleImageRun(b);
            if (img && EmitImagePara(c, img)) break;
            const MdRun* dm = SoleDisplayMathRun(b);
            if (dm && EmitMathPara(c, dm)) break;
            EmitPara(c, b, qd);
            break;
        }
        case MDB_H: {
            int lv = b->level < 1 ? 1 : (b->level > 6 ? 6 : b->level);
            Xs(x, "<w:p><w:pPr><w:pStyle w:val=\"Heading");
            Xnum(x, lv);
            Xs(x, "\"/></w:pPr>");
            ParaRuns(c, b->runs, b->nRuns, qd);
            Xs(x, "</w:p>");
            break;
        }
        case MDB_CODE: {
            if (b->diag) {          /* Mermaid：矢量 EMF 居中 */
                long long ew = 0, eh = 0;
                int rid = AddDiagram(c, b->diag, &ew, &eh);
                if (rid) {
                    Xs(x, "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>");
                    DrawingRun(c, rid, ew, eh);
                    Xs(x, "</w:p>");
                    return;
                }
                /* 渲染失败 → 按源码导出 */
            }
            Xs(x, "<w:p><w:pPr>"
                  "<w:pBdr>"
                  "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"2\" w:color=\"DDDDDD\"/>"
                  "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"4\" w:color=\"DDDDDD\"/>"
                  "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"2\" w:color=\"DDDDDD\"/>"
                  "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"4\" w:color=\"DDDDDD\"/>"
                  "</w:pBdr>"
                  "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F5F5F5\"/>"
                  "</w:pPr>");
            const wchar_t* p = b->code;
            BOOL first = TRUE;
            while (p && *p) {
                const wchar_t* nl = wcschr(p, L'\n');
                int len = nl ? (int)(nl - p) : (int)wcslen(p);
                while (len > 0 && p[len - 1] == L'\r') len--;
                if (!first) Xs(x, "<w:r><w:br/></w:r>");
                first = FALSE;
                if (len > 0) {
                    Xs(x, "<w:r><w:rPr>"
                          "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/>"
                          "</w:rPr><w:t xml:space=\"preserve\">");
                    XwSeg(x, p, len);
                    Xs(x, "</w:t></w:r>");
                }
                if (!nl) break;
                p = nl + 1;
            }
            Xs(x, "</w:p>");
            break;
        }
        case MDB_HR:
            Xs(x, "<w:p><w:pPr><w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" "
                  "w:space=\"1\" w:color=\"BFBFBF\"/></w:pBdr></w:pPr></w:p>");
            break;
        case MDB_QUOTE:
            for (int i = 0; i < b->nChildren; i++)
                WalkBlock(c, b->children[i], lvl, qd + 1);
            break;
        case MDB_UL:
        case MDB_OL:
            WalkList(c, b, 0, qd);
            break;
        case MDB_TABLE:
            WalkTable(c, b, qd);
            break;
        case MDB_HTML: {
            /* 原样不可用：退化为等宽源码段落 */
            Xs(x, "<w:p><w:r><w:rPr>"
                  "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\"/>"
                  "</w:rPr><w:t xml:space=\"preserve\">");
            Xw(x, b->code);
            Xs(x, "</w:t></w:r></w:p>");
            break;
        }
        case MDB_LI:
            WalkLi(c, b, lvl, qd, 0);
            break;
    }
}

/* ---------------- 部件模板 ---------------- */

static const char* kDocXmlHead =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
    "xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
    "<w:body>";

static const char* kDocXmlTail =
    "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
    "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" "
    "w:header=\"851\" w:footer=\"992\" w:gutter=\"0\"/></w:sectPr>"
    "</w:body></w:document>";

static const char* kStylesXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:docDefaults><w:rPrDefault><w:rPr>"
    "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"宋体\" w:cs=\"Times New Roman\"/>"
    "<w:sz w:val=\"21\"/><w:szCs w:val=\"21\"/>"
    "</w:rPr></w:rPrDefault>"
    "<w:pPrDefault><w:pPr><w:spacing w:after=\"120\"/></w:pPr></w:pPrDefault>"
    "</w:docDefaults>"
    "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
    "<w:name w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\">"
    "<w:name w:val=\"List Paragraph\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\">"
    "<w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"0\"/>"
    "<w:spacing w:before=\"240\" w:after=\"120\"/></w:pPr>"
    "<w:rPr><w:b/><w:color w:val=\"1F4E79\"/><w:sz w:val=\"36\"/><w:szCs w:val=\"36\"/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\">"
    "<w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"1\"/>"
    "<w:spacing w:before=\"200\" w:after=\"100\"/></w:pPr>"
    "<w:rPr><w:b/><w:color w:val=\"1F4E79\"/><w:sz w:val=\"30\"/><w:szCs w:val=\"30\"/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading3\">"
    "<w:name w:val=\"heading 3\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"2\"/>"
    "<w:spacing w:before=\"180\" w:after=\"100\"/></w:pPr>"
    "<w:rPr><w:b/><w:color w:val=\"1F4E79\"/><w:sz w:val=\"26\"/><w:szCs w:val=\"26\"/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading4\">"
    "<w:name w:val=\"heading 4\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"3\"/>"
    "<w:spacing w:before=\"160\" w:after=\"80\"/></w:pPr>"
    "<w:rPr><w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading5\">"
    "<w:name w:val=\"heading 5\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"4\"/>"
    "<w:spacing w:before=\"140\" w:after=\"80\"/></w:pPr>"
    "<w:rPr><w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading6\">"
    "<w:name w:val=\"heading 6\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"5\"/>"
    "<w:spacing w:before=\"120\" w:after=\"80\"/></w:pPr>"
    "<w:rPr><w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"21\"/><w:szCs w:val=\"21\"/></w:rPr></w:style>"
    "</w:styles>";

static const char* kRelsRoot =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
    "Target=\"word/document.xml\"/></Relationships>";

static const char* kContentTypes =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Default Extension=\"png\" ContentType=\"image/png\"/>"
    "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
    "<Default Extension=\"gif\" ContentType=\"image/gif\"/>"
    "<Default Extension=\"bmp\" ContentType=\"image/bmp\"/>"
    "<Default Extension=\"tiff\" ContentType=\"image/tiff\"/>"
    "<Default Extension=\"emf\" ContentType=\"image/x-emf\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>"
    "</Types>";

/* ---------------- 导出入口 ---------------- */

void MdExport_Docx(void) {
    if (g_curDoc < 0 || g_curDoc >= g_docCount) return;
    if (g_docs[g_curDoc].lang != LANG_MD) return;

    wchar_t fname[MAX_PATH];
    DWORD envLen = GetEnvironmentVariableW(L"ETON_EXPORT_PATH", fname, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        /* 自动化路径：环境变量指定输出文件 */
    } else {
        wcscpy_s(fname, MAX_PATH, g_docs[g_curDoc].title);
        wchar_t* dot = wcsrchr(fname, L'.');
        if (dot) *dot = 0;
        wcscat_s(fname, MAX_PATH, L".docx");
        OPENFILENAMEW ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_hwndMain;
        ofn.lpstrFilter = L"Word 文档 (*.docx)\0*.docx\0\0";
        ofn.lpstrFile = fname;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) return;
        if (!PathMatchSpecW(fname, L"*.docx"))
            wcscat_s(fname, MAX_PATH, L".docx");
    }

    const MdBlock* root = MdView_EnsureDocModel();
    if (!root) return;

    /* DCX 含约 1MB 的超链接表，必须放堆上（默认栈仅 1MB） */
    DCX* c = (DCX*)calloc(1, sizeof(DCX));
    if (!c) return;
    c->nextRid = 3;     /* rId1=styles, rId2=numbering */
    HDC ref = GetDC(NULL);
    c->fonts = ref ? MdView_DocFonts(ref) : NULL;

    for (int i = 0; i < root->nChildren; i++)
        WalkBlock(c, root->children[i], 0, 0);

    /* document.xml */
    XB doc = { 0 };
    Xs(&doc, kDocXmlHead);
    if (c->doc.p) XbPut(&doc, c->doc.p, c->doc.len);
    Xs(&doc, kDocXmlTail);

    /* numbering.xml：项目符号 + 按需生成的有序列表 */
    XB num = { 0 };
    Xs(&num, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    Xs(&num, "<w:abstractNum w:abstractNumId=\"0\">");
    static const char* bulletCh[3] = { "\xE2\x80\xA2", "\xE2\x97\x8B", "\xE2\x96\xAA" };
    static const int  bulletInd[3] = { 720, 1440, 2160 };
    for (int l = 0; l < 3; l++) {
        char t[256];
        sprintf_s(t, sizeof(t),
            "<w:lvl w:ilvl=\"%d\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/>"
            "<w:lvlText w:val=\"%s\"/><w:lvlJc w:val=\"left\"/>"
            "<w:pPr><w:ind w:left=\"%d\" w:hanging=\"360\"/></w:pPr></w:lvl>",
            l, bulletCh[l], bulletInd[l]);
        Xs(&num, t);
    }
    Xs(&num, "</w:abstractNum>");
    if (c->numAbs.p) XbPut(&num, c->numAbs.p, c->numAbs.len);
    for (int k = 0; k < c->nextNumId; k++) {
        char t[160];
        sprintf_s(t, sizeof(t),
            "<w:num w:numId=\"%d\"><w:abstractNumId w:val=\"%d\"/></w:num>",
            100 + k, 100 + k);
        Xs(&num, t);
    }
    Xs(&num, "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num></w:numbering>");

    /* document.xml.rels */
    XB rels = { 0 };
    Xs(&rels, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
              "<Relationship Id=\"rId1\" "
              "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
              "Target=\"styles.xml\"/>"
              "<Relationship Id=\"rId2\" "
              "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" "
              "Target=\"numbering.xml\"/>");
    if (c->rels.p) XbPut(&rels, c->rels.p, c->rels.len);
    Xs(&rels, "</Relationships>");

    BOOL ok = ZipAdd(&c->zip, "[Content_Types].xml", kContentTypes, (DWORD)strlen(kContentTypes)) &&
              ZipAdd(&c->zip, "_rels/.rels", kRelsRoot, (DWORD)strlen(kRelsRoot)) &&
              ZipAdd(&c->zip, "word/document.xml", doc.p, (DWORD)doc.len) &&
              ZipAdd(&c->zip, "word/styles.xml", kStylesXml, (DWORD)strlen(kStylesXml)) &&
              ZipAdd(&c->zip, "word/numbering.xml", num.p, (DWORD)num.len) &&
              ZipAdd(&c->zip, "word/_rels/document.xml.rels", rels.p, (DWORD)rels.len) &&
              ZipWriteFile(&c->zip, fname);

    free(doc.p); free(num.p); free(rels.p);
    free(c->doc.p); free(c->numAbs.p); free(c->rels.p);
    ZipFree(&c->zip);
    free(c);
    if (ref) ReleaseDC(NULL, ref);

    if (!ok) MessageBoxW(g_hwndMain, L"Failed to write .docx", L"ETON", MB_ICONERROR);
}

/* SAC verdict refresh: build 1790493093 */
