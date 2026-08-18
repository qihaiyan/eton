@echo off
REM ============================================================================
REM  eton - lightweight Windows text editor
REM  Core: Scintilla + Lexilla (statically linked)
REM  Build script: requires Visual Studio Build Tools (MSVC) and Windows SDK
REM
REM  Usage:
REM    build.bat          interactive mode (default): pauses at the end,
REM                       suitable for double-click or manual runs
REM    build.bat auto     non-interactive mode: no pause; prints BUILD_OK and
REM                       exits 0 on success, prints RCFAIL / CLFAIL /
REM                       VCVARSFAIL and exits non-zero on failure
REM                       (for automation / scripts / CI)
REM
REM  MSVC location (no hardcoded path):
REM    1) vcvarsall.bat pointed to by the VCVARS environment variable
REM    2) vswhere auto-detection (any drive / version / edition, incl. Build
REM       Tools)
REM    3) fallback scan of common install paths (when vswhere is unavailable)
REM ============================================================================
setlocal enabledelayedexpansion

REM ---------- arguments ----------
set "AUTO=0"
if /i "%~1"=="auto" set "AUTO=1"
if /i "%~1"=="now"  set "AUTO=1"
if "%AUTO%"=="1" goto :args_ok
if "%~1"=="" goto :args_ok
echo Error: unknown argument "%~1". Usage: build.bat [auto]
exit /b 2
:args_ok

REM ---------- locate vcvarsall.bat ----------
if not defined VCVARS goto :search_vswhere
if exist "%VCVARS%" goto :have_vcvars

:search_vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :search_common
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
)
if defined VCVARS goto :have_vcvars

:search_common
for %%y in (2026 18 2022 2019) do (
    for %%e in (Community Professional Enterprise BuildTools) do (
        for %%d in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
            if not defined VCVARS if exist "%%~d\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%~d\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvarsall.bat"
        )
    )
)
if defined VCVARS goto :have_vcvars

echo Error: vcvarsall.bat not found (MSVC environment).
echo Please install Visual Studio Build Tools with the "Desktop development
echo with C++" workload, or set the VCVARS environment variable to point to
echo vcvarsall.bat and retry, for example:
echo   set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
if "%AUTO%"=="1" ( echo VCVARSFAIL & endlocal & exit /b 1 )
pause
exit /b 1

:have_vcvars
echo Using MSVC environment: %VCVARS%
call "%VCVARS%" x64
if errorlevel 1 (
    echo Error: failed to initialize MSVC environment ^(vcvarsall.bat failed^).
    if "%AUTO%"=="1" ( echo VCVARSFAIL & endlocal & exit /b 1 )
    pause
    exit /b 1
)
cd /d "%~dp0"

if not exist build mkdir build
echo [1/3] compiling resources ...
rc /nologo /fo build\eton.res src\eton.rc
if errorlevel 1 goto :fail_rc
rc /nologo /fo build\app.res app.rc
if errorlevel 1 goto :fail_rc

echo [2/3] compiling + linking ...
cl /nologo /W3 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   /I"deps\scintilla" /I"deps\lexilla" ^
   /Fo"build/" /Fe:eton.exe ^
   src\main.c src\editor.c src\tabbar.c src\fileio.c src\dialogs.c src\jsonfmt.c src\session.c build\eton.res build\app.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /LIBPATH:"deps\scintilla" /LIBPATH:"deps\lexilla" ^
   libscintilla.lib liblexilla.lib ^
   user32.lib gdi32.lib comctl32.lib kernel32.lib shell32.lib shlwapi.lib comdlg32.lib imm32.lib ole32.lib oleaut32.lib
if errorlevel 1 goto :fail_cl

echo [3/3] done.
if "%AUTO%"=="1" ( echo BUILD_OK & endlocal & exit /b 0 )
echo.
echo Build OK: eton.exe  (Scintilla+Lexilla statically linked, portable)
pause
endlocal
exit /b 0

:fail_rc
echo Error: resource compilation failed ^(rc.exe^).
if "%AUTO%"=="1" ( echo RCFAIL & endlocal & exit /b 1 )
pause
exit /b 1

:fail_cl
echo Error: compile/link failed, see messages above.
if "%AUTO%"=="1" ( echo CLFAIL & endlocal & exit /b 1 )
pause
exit /b 1
