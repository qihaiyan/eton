# eton Development Rules

**Rule one: no comments in code. Never write `//` or `/* */` comments — not in new code, not in modified code, no exceptions.**

- Express intent through naming, structure, and commit messages. Background information that must be preserved (version differences, hard-won findings, non-obvious constraints) belongs in this file, never in code.
- Scope: all first-party source files in the repository (`src/` .c/.cpp/.h/.rc plus root-level tool sources). `deps/` contains third-party prebuilt libraries — never modify it.

## Known Findings (from past debugging — do not regress)

- Scintilla's DirectWrite backend selects CJK fallback fonts using the en-us locale by default, so Han characters fall back to a Japanese font with wrong glyph shapes (e.g. an isolated dot inside "类"; Scintilla bug #2027). `DwFontLocale()` in `editor.c` follows the system locale and falls back to zh-CN for non-CJK locales — do not remove.
- The Markdown preview (`dwtext.cpp`) maps CJK Han fallback to SimHei: Microsoft YaHei's "类" glyph has a vertical-stroke tip that protrudes outside the character box and reads as an extra dot; SimHei keeps every stroke inside the box.
- Bundled Lexilla markdown lexer style numbers: 2/3 strong, 4/5 emphasis, 6..11 are H1..H6, 12 pre-char marker, 13/14 list marker, 15 blockquote, 18 link, 19..21 inline code.

## Build and Verification

- Build: run `build.bat auto` from the repository root; success prints `BUILD_OK`. The output is `eton.exe` in the root (Scintilla/Lexilla statically linked).
- Current dependency: Scintilla 5.6.7 (2026-09-28), compiled from source into `deps/scintilla/libscintilla.lib`; the link line needs `d2d1.lib dwrite.lib`. Lexilla remains the repository's original version.
- For UI verification, use PowerShell screenshots (CopyFromScreen / PrintWindow) with pixel analysis; no extra dependencies.
