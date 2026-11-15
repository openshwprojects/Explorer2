@echo off
setlocal
cd /d "%~dp0"

if not "%E2_SKIP_BUILD%"=="1" (
    call "%~dp0build.bat"
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
)

if not exist "Explorer2.exe" (
    echo Explorer2.exe not found. Run build.bat first.
    exit /b 1
)

if not exist "build" mkdir "build"
if not "%E2_SKIP_GEN_VERSION%"=="1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gen_version.ps1"
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
)

set "E2_SEMVER=1.0.0"
set "E2_FILEVER=1.0.0.0"
if exist "build\version_vars.cmd" call "build\version_vars.cmd"

if exist "dist" rmdir /S /Q "dist"
mkdir "dist"

rem 1. Copy standalone versioned EXE
copy /Y "Explorer2.exe" "dist\Explorer2-%E2_SEMVER%-x64.exe" >nul
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

rem 2. Create portable ZIP package (Explorer2.exe + LICENSE + README.md)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$stage = 'build\portable_stage'; if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }; New-Item -ItemType Directory -Path $stage | Out-Null; Copy-Item 'Explorer2.exe','LICENSE','README.md' -Destination $stage; Compress-Archive -Path \"$stage\*\" -DestinationPath \"dist\Explorer2-%E2_SEMVER%-portable-x64.zip\" -Force; Remove-Item -Recurse -Force $stage"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

rem 3. Locate Inno Setup 6 (ISCC.exe) and compile Windows Setup installer
set "ISCC_EXE="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE (
    for /f "usebackq delims=" %%i in (`where iscc.exe 2^>nul`) do if not defined ISCC_EXE set "ISCC_EXE=%%i"
)

if not defined ISCC_EXE (
    echo Inno Setup 6 ^(ISCC.exe^) was not found. Install Inno Setup 6 to build the Setup installer.
    exit /b 1
)

"%ISCC_EXE%" /Qp "/DMyAppVersion=%E2_SEMVER%" "/DMyAppFileVersion=%E2_FILEVER%" "installer\Explorer2.iss"
if %ERRORLEVEL% NEQ 0 (
    echo Installer build failed with code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

rem 4. Generate SHA256SUMS.txt across all release artifacts in dist\
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$lines = Get-ChildItem 'dist' -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object { $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); \"$h  $($_.Name)\" }; [System.IO.File]::WriteAllLines((Join-Path (Resolve-Path 'dist') 'SHA256SUMS.txt'), $lines, (New-Object System.Text.UTF8Encoding($false)))"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Package build succeeded in dist\:
dir /B "dist"
exit /b 0
