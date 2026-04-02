@echo off
title MiniPHPServer Builder
color 0A

echo.
echo  ========================================
echo   MiniPHPServer v2.0 - Build
echo  ========================================
echo.

echo  [1/2] Compiling...
gcc main2_0.c -o MiniPHPServer.exe -lws2_32 -O2

if %errorlevel% neq 0 (
    echo  [ERROR] Compilation error!
    pause
    exit /b 1
)
echo        OK

echo  [2/2] Kopируji DLL...
for /f "delims=" %%i in ('where gcc') do set "GCC_EXE=%%i"
for %%i in ("%GCC_EXE%") do set "BIN_DIR=%%~dpi"

call :copydll libgcc_s_seh-1.dll
call :copydll libwinpthread-1.dll

echo.
echo  ========================================
echo   Done!
echo  ========================================
dir /b *.exe *.dll 2>nul
echo.
pause
exit /b 0

:copydll
if exist "%BIN_DIR%%~1" (
    copy /Y "%BIN_DIR%%~1" "%~1" >nul
    echo        OK: %~1
) else (
    echo        Neni potreba: %~1
)
exit /b 0
