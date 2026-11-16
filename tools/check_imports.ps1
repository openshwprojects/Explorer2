# Fails (exit 1) if Explorer2.exe would not start on Windows 7 SP1 or on "N" editions of Windows.
#  - No api-ms-win-* / ext-ms-win-* API-set imports: Windows 7 has none of them (they come in through umbrella
#    libraries such as WindowsApp.lib) and the loader then reports "api-ms-win-core-heap-l2-1-0.dll was not found".
#  - DLLs that are not on every edition (mfplat.dll is missing on Windows 7/10/11 N without the Media Feature
#    Pack) must be delay-loaded, never statically imported.
param([string]$Exe = "Explorer2.exe")

$ErrorActionPreference = "Stop"
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (-not $dumpbin) {
    Write-Host "check_imports: dumpbin.exe not on PATH (run from a VS developer prompt); import check skipped"
    exit 0
}

$lines = & $dumpbin.Source /nologo /dependents $Exe
$static = @()
$delay = @()
$section = ""
foreach ($l in $lines) {
    if ($l -match "Image has the following dependencies") { $section = "static"; continue }
    if ($l -match "Image has the following delay load dependencies") { $section = "delay"; continue }
    if ($l -match "^\s*Summary") { $section = ""; continue }
    if ($l -match "^\s+(\S+\.dll)\s*$") {
        if ($section -eq "static") { $static += $Matches[1] } elseif ($section -eq "delay") { $delay += $Matches[1] }
    }
}

if ($static.Count -eq 0) {
    Write-Host "check_imports: could not read the import table of $Exe"
    exit 1
}

$bad = @()
foreach ($d in ($static + $delay)) {
    if ($d -match "^(api|ext)-ms-win-") { $bad += "$d (API set, missing on Windows 7)" }
}
$mustDelay = @("mfplat.dll")
foreach ($d in $static) {
    if ($mustDelay -contains $d.ToLowerInvariant()) { $bad += "$d (statically imported; missing on Windows N editions - must be /DELAYLOAD)" }
}

if ($bad.Count -gt 0) {
    Write-Host "Build failed: $Exe would not start on every supported Windows:"
    $bad | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host ("check_imports: OK ({0} static, {1} delay-loaded: {2})" -f $static.Count, $delay.Count, ($delay -join ", "))
exit 0
