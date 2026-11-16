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
if not "%E2_SKIP_GEN_VERSION%"=="1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gen_version.ps1" >nul 2>&1
)
set "E2_VER_RC="
set "E2_VER_CL="
if exist build\version_gen.h (
    set "E2_VER_RC=/i build /i src /d E2_HAS_GENERATED_VERSION"
    set "E2_VER_CL=/Ibuild /DE2_HAS_GENERATED_VERSION"
) else (
    set "E2_VER_RC=/i src"
)
del /Q build\Explorer2.running*.bak >nul 2>&1
if exist Explorer2.exe move /Y Explorer2.exe build\Explorer2.running.%RANDOM%_%TIME:~6,2%.bak >nul 2>&1

rc.exe /nologo %E2_VER_RC% /fo build\Explorer2.res src\Explorer2.rc
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cl.exe /nologo /MP /std:c++20 /O2 /utf-8 /EHsc /W3 %E2_VER_CL% ^
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
    src\FormatTests3D.cpp ^
    src\FormatTestsEda.cpp ^
    src\FormatTestsImages.cpp ^
    src\FormatTestsDocs.cpp ^
    src\FormatTestsDocsB.cpp ^
    src\FormatTestsArchives.cpp ^
    src\FormatTestsBatch70.cpp ^
    src\ExplorerApp.cpp ^
    src\ExplorerAppRender.cpp ^
    /Fo:build\ ^
    /Fe:Explorer2.exe ^
    /link /SUBSYSTEM:WINDOWS build\Explorer2.res ^
    kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib comctl32.lib d2d1.lib dwrite.lib windowscodecs.lib mfplat.lib mfuuid.lib wininet.lib ws2_32.lib cfgmgr32.lib crypt32.lib bcrypt.lib setupapi.lib version.lib
set "E2_LINK_RC=%ERRORLEVEL%"

rem Windows 7 guard: never link WindowsApp.lib (or other umbrella libs). They import kernel32 functions through
rem api-ms-win-core-*.dll API sets that Windows 7 does not have, and the exe then fails to start there with
rem "api-ms-win-core-heap-l2-1-0.dll was not found". Fail the build if any such import sneaks back in.
if "%E2_LINK_RC%"=="0" (
    dumpbin.exe /nologo /imports Explorer2.exe | findstr /i /r /c:"^ *api-ms-win-[a-z0-9-]*\.dll" /c:"^ *ext-ms-win-[a-z0-9-]*\.dll" >build\apiset_imports.txt
    if not errorlevel 1 (
        echo Build failed: Explorer2.exe imports Windows 8+ API-set DLLs and would not start on Windows 7:
        type build\apiset_imports.txt
        exit /b 1
    )
)

if "%E2_LINK_RC%"=="0" (
    del /Q build\Explorer2_v6.running*.bak >nul 2>&1
    if exist Explorer2_v6.exe move /Y Explorer2_v6.exe build\Explorer2_v6.running.%RANDOM%_%TIME:~6,2%.bak >nul 2>&1
    copy /Y Explorer2.exe Explorer2_v6.exe >nul 2>&1
    del /Q build\Explorer2.running.bak >nul 2>&1
    echo Build succeeded: Explorer2.exe
) else (
    echo Build failed with code %E2_LINK_RC%
    exit /b %E2_LINK_RC%
)
exit /b 0

:no_toolchain
echo Visual Studio with the "Desktop development with C++" workload was not found.
echo Install it, or run build.bat from an "x64 Native Tools Command Prompt for VS".
exit /b 1
