@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo VCVARS_FAIL
    exit /b 1
)
call "D:\workspace\eton\build\make-deps.cmd" "C:\Users\haiyan\AppData\Local\Temp\sciup" "C:\Users\haiyan\AppData\Local\Temp\sciup\out"
