@echo off
setlocal
cd /d "%~dp0"

rem Find the MSVC x64 toolchain. Nothing to do inside an x64 developer prompt; otherwise ask vswhere
rem for the newest Visual Studio (any edition, Build Tools included) that has the C++ tools.
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" goto toolchain_ready
set "VSINSTALL="
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSINSTALL=%%i"
if not defined VSINSTALL set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto no_toolchain
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
:toolchain_ready

if not exist build mkdir build
del /Q build\Explorer2.running*.bak >nul 2>&1
if exist Explorer2.exe move /Y Explorer2.exe build\Explorer2.running.%RANDOM%_%TIME:~6,2%.bak >nul 2>&1

rc.exe /nologo /fo build\Explorer2.res src\Explorer2.rc
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /nologo /MP /std:c++20 /O2 /utf-8 /EHsc /W3 ^
    src\main.cpp ^
    src\FileEngine.cpp ^
    src\ImageFormatLoader.cpp ^
    src\ImageCache.cpp ^
    src\ShellMenuEmulator.cpp ^
    src\MediaPlayer.cpp ^
    src\HardwareInfo.cpp ^
    src\ProgramsInfo.cpp ^
    src\Monitoring.cpp ^
    src\LanScanner.cpp ^
    src\GitIntegration.cpp ^
    src\AppLog.cpp ^
    src\ZipDeflate.cpp ^
    src\NotepadViewer.cpp ^
    src\HexViewer.cpp ^
    src\Model3DViewer.cpp ^
    src\EdaViewer.cpp ^
    src\SpreadsheetDocViewer.cpp ^
    src\ArchiveExplorer.cpp ^
    src\PdfViewer.cpp ^
    src\BackupManager.cpp ^
    src\TransferManager.cpp ^
    src\FtpClient.cpp ^
    src\FileContentPane.cpp ^
    src\SelfTestSuite.cpp ^
    src\ExplorerApp.cpp ^
    src\ExplorerAppRender.cpp ^
    /Fo:build\ ^
    /Fe:Explorer2.exe ^
    /link /SUBSYSTEM:WINDOWS build\Explorer2.res ^
    user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib comctl32.lib d2d1.lib dwrite.lib windowscodecs.lib mfplat.lib mfuuid.lib wininet.lib WindowsApp.lib Shcore.lib

if %ERRORLEVEL% EQU 0 (
    del /Q build\Explorer2_v6.running*.bak >nul 2>&1
    if exist Explorer2_v6.exe move /Y Explorer2_v6.exe build\Explorer2_v6.running.%RANDOM%_%TIME:~6,2%.bak >nul 2>&1
    copy /Y Explorer2.exe Explorer2_v6.exe >nul 2>&1
    del /Q build\Explorer2.running.bak >nul 2>&1
    echo Build succeeded: Explorer2.exe
) else (
    echo Build failed with code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
exit /b 0

:no_toolchain
echo Visual Studio with the "Desktop development with C++" workload was not found.
echo Install it, or run build.bat from an "x64 Native Tools Command Prompt for VS".
exit /b 1
