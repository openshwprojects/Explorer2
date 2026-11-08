@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build mkdir build

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
    src\BackupManager.cpp ^
    src\FtpClient.cpp ^
    src\FileContentPane.cpp ^
    src\SelfTestSuite.cpp ^
    src\ExplorerApp.cpp ^
    src\ExplorerAppRender.cpp ^
    /Fo:build\ ^
    /Fe:Explorer2.exe ^
    /link /SUBSYSTEM:WINDOWS ^
    user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib comctl32.lib d2d1.lib dwrite.lib windowscodecs.lib mfplat.lib mfuuid.lib wininet.lib

if %ERRORLEVEL% EQU 0 (
    echo Build succeeded: w:\GIT\Explorer2\Explorer2.exe
) else (
    echo Build failed with code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
