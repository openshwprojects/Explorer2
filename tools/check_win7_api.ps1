# Windows 7 / 8 API compatibility check for Explorer2.exe (needs PowerShell 7+: pwsh).
#
# GitHub has no Windows 7 or 8 runners, so this checks the built exe statically instead. It fails when the exe
# would not start, or would hit a missing function, on the oldest Windows it is meant to run on (-MinOs, default
# 6.1 = Windows 7 SP1; use 6.2 for Windows 8, 6.3 for Windows 8.1):
#   1. PE header: "operating system version" / "subsystem version" must not be newer than -MinOs.
#   2. Static DLL imports: no API-set DLLs (api-ms-win-* / ext-ms-win-*) and no DLLs that only ship with
#      Windows 8+ (combase, shcore, ...). Windows refuses to start the exe if one of them is missing.
#   3. Every statically imported function is looked up in Microsoft's Win32 API metadata
#      (Microsoft.Windows.SDK.Win32Metadata, the "Minimum supported client" of the docs as
#      [SupportedOSPlatform]) and must not need a newer Windows than -MinOs. A function the loader cannot find
#      stops the exe from starting, exactly like a missing DLL.
#   Delay-loaded imports are reported only: they are loaded on first use, and the code must check for them first
#   (see src/OsCompat.h). Functions the metadata has no version for are listed as "unknown" and do not fail.
#   Known false positives can be listed in tools/win7_api_allowlist.txt ("FunctionName  # reason").
#
# Usage:  pwsh -File tools/check_win7_api.ps1 [-Exe Explorer2.exe] [-MinOs 6.1]
param(
    [string]$Exe = "Explorer2.exe",
    [string]$MinOs = "6.1",
    [string]$Winmd = "",
    [string]$MetadataVersion = "71.0.45-preview",
    [string]$CacheDir = "build/win32metadata"
)

$ErrorActionPreference = "Stop"
if ($PSVersionTable.PSEdition -ne "Core") {
    Write-Host "check_win7_api: needs PowerShell 7+ (pwsh) for System.Reflection.Metadata. Run: pwsh -File $PSCommandPath"
    exit 2
}

$minVersion = [version]$MinOs
$osNames = @{ "6.0" = "Windows Vista"; "6.1" = "Windows 7"; "6.2" = "Windows 8"; "6.3" = "Windows 8.1"; "10.0" = "Windows 10" }
$minName = if ($osNames.ContainsKey($MinOs)) { $osNames[$MinOs] } else { "Windows $MinOs" }
$failures = [System.Collections.Generic.List[string]]::new()
$notes = [System.Collections.Generic.List[string]]::new()

# ---- dumpbin --------------------------------------------------------------------------------------------------
$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if (-not $dumpbin) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) {
            $dumpbin = Get-ChildItem (Join-Path $vs "VC\Tools\MSVC") -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "Hostx64\\x64" } | Select-Object -First 1 -ExpandProperty FullName
        }
    }
}
if (-not $dumpbin) { Write-Host "check_win7_api: dumpbin.exe not found (run from a VS developer prompt)"; exit 2 }
if (-not (Test-Path $Exe)) { Write-Host "check_win7_api: $Exe not found"; exit 2 }

# ---- 1. PE header ---------------------------------------------------------------------------------------------
$headers = & $dumpbin /nologo /headers $Exe
foreach ($kind in "operating system version", "subsystem version") {
    $line = $headers | Where-Object { $_ -match "^\s*([\d.]+)\s+$kind\s*$" } | Select-Object -First 1
    if ($line -and $line -match "^\s*([\d.]+)\s+") {
        $v = [version]$Matches[1]
        if ($v -gt $minVersion) { $failures.Add("PE header $kind is $v (newer than $minName $MinOs): Windows refuses to start the exe") }
    }
}

# ---- 2. imports -----------------------------------------------------------------------------------------------
$importLines = & $dumpbin /nologo /imports $Exe
$imports = [System.Collections.Generic.List[object]]::new()     # @{ Dll; Name; Delay }
$dll = $null; $delay = $false
foreach ($l in $importLines) {
    if ($l -match "Section contains the following delay load imports") { $delay = $true; $dll = $null; continue }
    if ($l -match "^\s*Summary\s*$") { break }
    if ($l -match "^\s{4}(\S+\.dll)\s*$") { $dll = $Matches[1]; continue }
    if ($dll -and $l -match "^\s+(?:[0-9A-Fa-f]+\s+)?[0-9A-Fa-f]+\s+([A-Za-z_][\w@?$]*)\s*$" -and $Matches[1] -ne "Characteristics") {
        $imports.Add([pscustomobject]@{ Dll = $dll; Name = $Matches[1]; Delay = $delay })
    } elseif ($dll -and $l -match "^\s+Ordinal\s+(\d+)\s*$") {
        $imports.Add([pscustomobject]@{ Dll = $dll; Name = "#" + $Matches[1]; Delay = $delay })
    }
}
if ($imports.Count -eq 0) { Write-Host "check_win7_api: could not read the import table of $Exe"; exit 2 }

# DLLs that a Windows 7 system does not have at all (static import = "...dll was not found" at start)
$win8PlusDlls = @("combase.dll", "shcore.dll", "dcomp.dll", "d3d12.dll", "dxcore.dll", "wintypes.dll",
                  "coremessaging.dll", "windows.storage.dll", "mfmediaengine.dll", "d3d11on12.dll", "dwmcore.dll")
$staticDlls = $imports | Where-Object { -not $_.Delay } | ForEach-Object { $_.Dll } | Sort-Object -Unique
$delayDlls = $imports | Where-Object { $_.Delay } | ForEach-Object { $_.Dll } | Sort-Object -Unique
foreach ($d in $staticDlls) {
    $dl = $d.ToLowerInvariant()
    if ($dl -match "^(api|ext)-ms-win-") { $failures.Add("static import of API-set DLL $d (Windows 7/8 lack most API sets, e.g. heap-l2-1-0 or any -l1-2-x; they come in through umbrella libs such as WindowsApp.lib - link kernel32.lib etc. instead)") }
    elseif ($win8PlusDlls -contains $dl) { $failures.Add("static import of $d (only ships with Windows 8 or later)") }
}

# ---- 3. Win32 metadata ----------------------------------------------------------------------------------------
if (-not $Winmd) {
    $Winmd = Join-Path $CacheDir "$MetadataVersion/Windows.Win32.winmd"
    if (-not (Test-Path $Winmd)) {
        New-Item -ItemType Directory -Force (Split-Path $Winmd) | Out-Null
        $pkg = Join-Path $CacheDir "win32metadata.$MetadataVersion.nupkg"
        $url = "https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.win32metadata/$MetadataVersion/microsoft.windows.sdk.win32metadata.$MetadataVersion.nupkg"
        Write-Host "check_win7_api: downloading Win32 metadata $MetadataVersion"
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $url -OutFile $pkg -TimeoutSec 180
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $pkg))
        try {
            $entry = $zip.Entries | Where-Object { $_.Name -eq "Windows.Win32.winmd" } | Select-Object -First 1
            if (-not $entry) { throw "Windows.Win32.winmd not in $pkg" }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path (Resolve-Path (Split-Path $Winmd)) "Windows.Win32.winmd"), $true)
        } finally { $zip.Dispose() }
        Remove-Item $pkg -Force
    }
}

$wanted = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($i in $imports) { [void]$wanted.Add($i.Name) }

# name -> list of @{ Dll; Os }
$known = @{}
$fs = [System.IO.File]::OpenRead((Resolve-Path $Winmd))
try {
    $pe = [System.Reflection.PortableExecutable.PEReader]::new($fs)
    $md = [System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
    $HK = [System.Reflection.Metadata.HandleKind]
    foreach ($h in $md.MethodDefinitions) {
        $m = $md.GetMethodDefinition($h)
        $imp = $m.GetImport()
        if ($imp.Module.IsNil) { continue }
        $name = $md.GetString($imp.Name)
        if (-not $wanted.Contains($name)) { continue }
        $mdll = $md.GetString($md.GetModuleReference($imp.Module).Name)
        $os = $null
        foreach ($cah in $m.GetCustomAttributes()) {
            $ca = $md.GetCustomAttribute($cah)
            $typeName = ""
            if ($ca.Constructor.Kind -eq $HK::MemberReference) {
                $parent = $md.GetMemberReference([System.Reflection.Metadata.MemberReferenceHandle]$ca.Constructor).Parent
                if ($parent.Kind -eq $HK::TypeReference) { $typeName = $md.GetString($md.GetTypeReference([System.Reflection.Metadata.TypeReferenceHandle]$parent).Name) }
                elseif ($parent.Kind -eq $HK::TypeDefinition) { $typeName = $md.GetString($md.GetTypeDefinition([System.Reflection.Metadata.TypeDefinitionHandle]$parent).Name) }
            } elseif ($ca.Constructor.Kind -eq $HK::MethodDefinition) {
                $typeName = $md.GetString($md.GetTypeDefinition($md.GetMethodDefinition([System.Reflection.Metadata.MethodDefinitionHandle]$ca.Constructor).GetDeclaringType()).Name)
            }
            if ($typeName -eq "SupportedOSPlatformAttribute") {
                $br = $md.GetBlobReader($ca.Value)
                [void]$br.ReadUInt16()
                $os = $br.ReadSerializedString()
            }
        }
        if (-not $known.ContainsKey($name)) { $known[$name] = [System.Collections.Generic.List[object]]::new() }
        $known[$name].Add([pscustomobject]@{ Dll = $mdll; Os = $os })
    }
} finally { $fs.Dispose() }

function ConvertTo-OsVersion([string]$s) {
    # "windows6.0.6000" -> 6.0, "windows10.0.14393" -> 10.0.14393: the target is a major.minor release, so a
    # build number only counts when the target names one too (else 6.0.6000 would look newer than 6.0)
    if ($s -match "^windows(\d+)(?:\.(\d+))?(?:\.(\d+))?") {
        $major = [int]$Matches[1]; $minor = if ($Matches[2]) { [int]$Matches[2] } else { 0 }
        # The metadata names Windows 8 / 8.1 by their marketing numbers ("windows8.0", "windows8.1"), everything
        # else by NT version (windows6.1 = 7, windows10.0 = 10): map 8.x to NT 6.2 / 6.3 so -MinOs 6.2 works
        if ($major -eq 8) { $major = 6; $minor = 2 + $minor }
        if ($minVersion.Build -ge 0 -and $Matches[3]) { return [version]::new($major, $minor, [int]$Matches[3]) }
        return [version]::new($major, $minor)
    }
    return $null
}

$allow = @{}
$allowFile = Join-Path $PSScriptRoot "win7_api_allowlist.txt"
if (Test-Path $allowFile) {
    foreach ($l in Get-Content $allowFile) {
        $t = ($l -replace "#.*$", "").Trim()
        if ($t) { $allow[$t] = $true }
    }
}

$unknown = [System.Collections.Generic.List[string]]::new()      # not in the metadata at all
$unversioned = 0                                                 # in the metadata without a minimum (pre-XP APIs)
$delayNewer = [System.Collections.Generic.List[string]]::new()
foreach ($i in $imports) {
    if ($i.Name.StartsWith("#")) { $unknown.Add("$($i.Dll)!$($i.Name) (by ordinal)"); continue }
    $entries = $known[$i.Name]
    if (-not $entries) { $unknown.Add("$($i.Dll)!$($i.Name)"); continue }
    # Prefer the metadata entry for the same DLL; otherwise the oldest version any entry claims
    $same = $entries | Where-Object { $_.Dll -ieq $i.Dll -and $_.Os } | Select-Object -First 1
    $versions = if ($same) { @(ConvertTo-OsVersion $same.Os) } else { @($entries | Where-Object { $_.Os } | ForEach-Object { ConvertTo-OsVersion $_.Os } | Where-Object { $_ }) }
    if ($versions.Count -eq 0) { $unversioned++; continue }
    $need = ($versions | Sort-Object | Select-Object -First 1)
    if ($need -le $minVersion) { continue }
    $nk = "$($need.Major).$($need.Minor)"
    $needName = if ($osNames.ContainsKey($nk)) { "$($osNames[$nk]) ($need)" } else { "Windows $need" }
    if ($allow.ContainsKey($i.Name)) { $notes.Add("allowlisted: $($i.Dll)!$($i.Name) (metadata says $needName)"); continue }
    if ($i.Delay) { $delayNewer.Add("$($i.Dll)!$($i.Name) needs $needName (delay-loaded: code must check before calling)") }
    else { $failures.Add("$($i.Dll)!$($i.Name) needs $needName; on $minName the exe would not start (entry point not found)") }
}

# ---- report ---------------------------------------------------------------------------------------------------
$staticCount = ($imports | Where-Object { -not $_.Delay }).Count
$delayCount = ($imports | Where-Object { $_.Delay }).Count
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("Target: $minName ($MinOs) - $Exe")
$report.Add("Static imports: $staticCount functions from $($staticDlls.Count) DLLs; delay-loaded: $delayCount functions from $($delayDlls.Count) DLLs ($($delayDlls -join ', '))")
$report.Add("Checked against Win32 metadata $MetadataVersion; $unversioned without a minimum version (pre-XP APIs, fine); not in the metadata (not checked): $($unknown.Count)")
foreach ($n in $notes) { $report.Add("  note: $n") }
foreach ($d in $delayNewer) { $report.Add("  delay-loaded: $d") }
if ($unknown.Count -gt 0) { $report.Add("  unknown: " + ($unknown -join ", ")) }
if ($failures.Count -gt 0) {
    $report.Add("FAILED - $($failures.Count) problem(s):")
    foreach ($f in $failures) { $report.Add("  $f") }
} else {
    $report.Add("OK - nothing in the import table needs a Windows newer than $minName")
}
$report | ForEach-Object { Write-Host $_ }

if ($env:GITHUB_STEP_SUMMARY) {
    $md = @("### $minName compatibility ($Exe)", "", '```') + $report + @('```', "")
    $md -join "`n" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Encoding utf8 -Append
}
if ($env:GITHUB_ACTIONS -eq "true") {
    foreach ($f in $failures) { Write-Host "::error title=$minName compatibility::$f" }
}
exit ([int]($failures.Count -gt 0))
