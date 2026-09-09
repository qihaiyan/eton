@echo off
REM Build Scintilla 5.6.6 + Lexilla 5.4.9 static libs. ASCII only (cmd codepage).
REM Usage: make-deps.cmd <src-root> <out-dir>
setlocal
set "SRC=%~1"
set "OUT=%~2"
if "%SRC%"=="" set "SRC=%~dp0sciup"
if "%OUT%"=="" set "OUT=%~dp0sciup\out"
if not exist "%OUT%" mkdir "%OUT%"
set CXXFLAGS=/c /MT /O2 /EHsc /std:c++20 /DNDEBUG /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DSTATIC_BUILD=1 /nologo

pushd "%OUT%"

echo == [1/4] Scintilla core (src) ==
cl %CXXFLAGS% /I"%SRC%\scintilla\include" /I"%SRC%\scintilla\src" /I"%SRC%\scintilla\win32" "%SRC%\scintilla\src\*.cxx" || goto :fail
echo == [2/4] Scintilla win32 platform ==
cl %CXXFLAGS% /I"%SRC%\scintilla\include" /I"%SRC%\scintilla\src" /I"%SRC%\scintilla\win32" "%SRC%\scintilla\win32\PlatWin.cxx" "%SRC%\scintilla\win32\ScintillaWin.cxx" "%SRC%\scintilla\win32\ListBox.cxx" "%SRC%\scintilla\win32\SurfaceD2D.cxx" "%SRC%\scintilla\win32\SurfaceGDI.cxx" "%SRC%\scintilla\win32\HanjaDic.cxx" || goto :fail
lib /nologo /out:libscintilla.lib *.obj || goto :fail
del *.obj

echo == [3/4] Lexilla src + lexlib ==
cl %CXXFLAGS% /I"%SRC%\lexilla\include" /I"%SRC%\lexilla\src" /I"%SRC%\lexilla\lexlib" /I"%SRC%\lexilla\lexers" /I"%SRC%\scintilla\include" "%SRC%\lexilla\src\*.cxx" "%SRC%\lexilla\lexlib\*.cxx" || goto :fail
echo == [4/4] Lexilla lexers ==
cl %CXXFLAGS% /I"%SRC%\lexilla\include" /I"%SRC%\lexilla\src" /I"%SRC%\lexilla\lexlib" /I"%SRC%\lexilla\lexers" /I"%SRC%\scintilla\include" "%SRC%\lexilla\lexers\*.cxx" || goto :fail
lib /nologo /out:liblexilla.lib *.obj || goto :fail
del *.obj

popd
echo DEPS_OK
exit /b 0
:fail
popd
echo DEPS_FAIL
exit /b 1
