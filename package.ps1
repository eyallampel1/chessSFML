<#
.SYNOPSIS
    Builds a Release copy of chessSFML and lays out a self-contained folder
    that runs on a clean Windows 10/11 machine, then zips it.

.DESCRIPTION
    The published folder needs, beyond the executable:

      openal32.dll        SFML's audio backend; it has no static variant
      msvcp140.dll        the Visual C++ runtime. SFML 2.6.2's static libs are
      vcruntime140.dll    built against the DYNAMIC CRT (/MD), so /MT is not an
      vcruntime140_1.dll  option without rebuilding SFML from source. These
                          three are redistributable app-locally.

    Not needed: sqlite3.dll (the amalgamation is compiled in) and the UCRT
    (part of Windows 10 and later).

.EXAMPLE
    .\package.ps1

.EXAMPLE
    .\package.ps1 -CourseDir "C:\...\Hakobyan 1.d4 - Chesscom" -Version 1.1
#>

[CmdletBinding()]
param(
    # Folder of course PGNs to bundle. Omit to ship without a course; the app
    # asks for a folder on first use either way.
    [string]$CourseDir,

    [string]$Version = "1.0",

    [string]$OutDir = (Join-Path $PSScriptRoot "dist"),

    # Reuse whatever is already in build\bin instead of rebuilding.
    [switch]$SkipBuild,

    [switch]$NoZip
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo    = $PSScriptRoot
$appName = "LampelChess"
$stage   = Join-Path $OutDir $appName

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Warn($text) { Write-Host "    ! $text" -ForegroundColor Yellow }

# ---------------------------------------------------------------- build ----

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - is Visual Studio installed?" }

$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { throw "No Visual Studio installation with MSBuild was found." }

if (-not $SkipBuild) {
    Step "Building Release|x64"

    $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) { throw "MSBuild.exe not found under $vsPath" }

    & $msbuild (Join-Path $repo "chessSFML.vcxproj") /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
} else {
    Step "Skipping build (-SkipBuild)"
}

$exe = Join-Path $repo "build\bin\LampelChess.exe"
if (-not (Test-Path $exe)) { throw "LampelChess.exe not found at $exe" }

# ---------------------------------------------------------------- stage ----

Step "Staging into $stage"

# The staged folder is rebuilt from scratch every run, but course/ holds the
# user's own PGNs and is theirs, not ours. It is carried across the rebuild
# untouched, always - the try/finally puts it back even if staging throws.
$courseDest = Join-Path $stage "course"
$parked = $null
if (Test-Path $courseDest) {
    $parked = Join-Path $OutDir ".course-parked"
    if (Test-Path $parked) { Remove-Item $parked -Recurse -Force }
    Move-Item $courseDest $parked
}

try {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Force $stage | Out-Null
} finally {
    if ($parked -and (Test-Path $parked)) {
        New-Item -ItemType Directory -Force $stage | Out-Null
        Move-Item $parked $courseDest
        Write-Host "    kept $(@(Get-ChildItem $courseDest -Filter *.pgn).Count) course PGNs already in the package"
    }
}

Copy-Item $exe $stage

# Debug and Release share build\bin\LampelChess.exe, so a stale -SkipBuild run can
# stage a debug binary. Its CRT is not redistributable and would not start on a
# machine without Visual Studio, so catch it here rather than at the customer.
$bytes = [System.IO.File]::ReadAllBytes($exe)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
if ($ascii -match "VCRUNTIME140D\.dll|ucrtbased\.dll") {
    throw "build\bin\LampelChess.exe is a DEBUG build - rerun without -SkipBuild."
}
Remove-Variable bytes, ascii

# --- SFML's OpenAL backend
$openal = "C:\SFML-2.6.2\bin\openal32.dll"
if (Test-Path $openal) {
    Copy-Item $openal $stage
} else {
    Warn "openal32.dll not found at $openal - sound will not work on the target machine"
}

# --- Visual C++ runtime, copied app-locally
#
# Must be at least as new as the toolset that compiled LampelChess.exe. Shipping an
# older one loads fine and then crashes with an access violation, so candidates
# are ranked by the actual file version of msvcp140.dll - never by path name,
# which would sort a "...\2022\..." install above a newer "...\18\..." one.
Step "Collecting the Visual C++ runtime"
$crtNames = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")

$crtCandidates = @()
foreach ($base in @($vsPath, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    if (-not $base) { continue }
    $pattern = if ($base -eq $vsPath) { "$base\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" }
               else { "$base\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" }
    $crtCandidates += Get-ChildItem $pattern -Directory -ErrorAction SilentlyContinue |
                      Select-Object -ExpandProperty FullName
}
$crtCandidates += (Join-Path $env:SystemRoot "System32")   # last resort

$best = $null
foreach ($dir in ($crtCandidates | Select-Object -Unique)) {
    if (@($crtNames | Where-Object { Test-Path (Join-Path $dir $_) }).Count -ne $crtNames.Count) { continue }
    $raw = (Get-Item (Join-Path $dir "msvcp140.dll")).VersionInfo.FileVersion
    $ver = $null
    if (-not [version]::TryParse(($raw -split ' ')[0], [ref]$ver)) { continue }
    if (-not $best -or $ver -gt $best.Version) {
        $best = [pscustomobject]@{ Dir = $dir; Version = $ver }
    }
}

if (-not $best) { throw "Could not find msvcp140.dll / vcruntime140*.dll anywhere to redistribute." }

Write-Host "    v$($best.Version) from $($best.Dir)"
foreach ($n in $crtNames) { Copy-Item (Join-Path $best.Dir $n) $stage -Force }

# The exe was linked against this machine's toolset, so anything older than
# what System32 carries is a red flag worth stopping for.
$sysMsvcp = Join-Path $env:SystemRoot "System32\msvcp140.dll"
if (Test-Path $sysMsvcp) {
    $sysVer = $null
    if ([version]::TryParse((((Get-Item $sysMsvcp).VersionInfo.FileVersion) -split ' ')[0], [ref]$sysVer)) {
        if ($best.Version -lt $sysVer) {
            throw ("Refusing to ship VC runtime v$($best.Version) when this machine runs v$sysVer - " +
                   "LampelChess.exe was built against the newer one and would crash. Install the matching redist.")
        }
    }
}

# --- data the app loads at runtime
Step "Copying assets and engine"
$assets = Join-Path $stage "src\assets"
New-Item -ItemType Directory -Force $assets | Out-Null
Copy-Item (Join-Path $repo "src\assets\*") $assets -Recurse -Force

$engines = Join-Path $stage "engines"
New-Item -ItemType Directory -Force $engines | Out-Null
if (Test-Path (Join-Path $repo "engines\stockfish.exe")) {
    Copy-Item (Join-Path $repo "engines\stockfish.exe") $engines
} else {
    Warn "engines\stockfish.exe not found - analysis will be disabled"
}

if ($CourseDir) {
    if (-not (Test-Path $CourseDir)) { throw "Course folder not found: $CourseDir" }
    Step "Adding course PGNs from $CourseDir"
    New-Item -ItemType Directory -Force $courseDest | Out-Null
    # Added to whatever is already there rather than replacing it.
    Copy-Item (Join-Path $CourseDir "*.pgn") $courseDest -Force
}

$coursePgns = @()
if (Test-Path $courseDest) {
    $coursePgns = @(Get-ChildItem $courseDest -Filter *.pgn -ErrorAction SilentlyContinue)
}
if ($coursePgns.Count -gt 0) {
    Write-Host "    course/ holds $($coursePgns.Count) PGNs"
}

# ---------------------------------------------------------------- notes ----

$licenses = Join-Path $stage "LICENSES"
New-Item -ItemType Directory -Force $licenses | Out-Null

@"
Third-party components shipped with $appName
============================================

Stockfish (engines/stockfish.exe)
    GNU General Public License v3. Stockfish runs as a separate process and
    talks UCI over a pipe, so it does not affect the licence of this
    application, but the binary may only be redistributed together with a copy
    of the GPLv3 and an offer of its source code.
    Source: https://github.com/official-stockfish/Stockfish
    Licence: https://www.gnu.org/licenses/gpl-3.0.txt

Chess piece artwork (src/assets/*.png)
    "Cburnett" SVG chess pieces by Colin M.L. Burnett, from Wikimedia Commons,
    re-rendered at 330px. Creative Commons Attribution-ShareAlike 3.0 Unported.
    https://commons.wikimedia.org/wiki/Category:SVG_chess_pieces
    https://creativecommons.org/licenses/by-sa/3.0/
    Attribution is required, and redistributed copies must keep this licence.

SFML 2.6.2 (linked statically) and openal32.dll
    zlib/png licence. https://www.sfml-dev.org/license.php

SQLite 3.50.4 (compiled in from the amalgamation)
    Public domain. https://sqlite.org/copyright.html

Microsoft Visual C++ runtime (msvcp140.dll, vcruntime140*.dll)
    Redistributed under the Visual Studio redistributable licence terms.
"@ | Set-Content (Join-Path $licenses "THIRD-PARTY.txt") -Encoding utf8

@"
$appName $Version
=================

Run LampelChess.exe. Everything it needs is in this folder - no installer and no
runtime to install first. Requires 64-bit Windows 10 or later.

Your progress (puzzle rating and the opening-course review schedule) is kept in
userdb.db next to the executable. Back up or copy that one file to move your
progress to another machine.

The opening-course mode asks for a folder of chapter PGNs the first time you
open it, and remembers the choice$(if ($CourseDir) { "; one is bundled in .\course" }).

See LICENSES\THIRD-PARTY.txt for the components shipped alongside.
"@ | Set-Content (Join-Path $stage "README.txt") -Encoding utf8

# ------------------------------------------------------------------ zip ----

$size = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
Step ("Staged {0:N1} MB" -f ($size / 1MB))

if (-not $NoZip) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem | Out-Null

    # Zips are built by picking entries, so the staged folder is never moved or
    # emptied to shape one. Everything lands under "<appName>/" in the archive,
    # the way Compress-Archive would have nested it.
    function New-Package([string]$zipPath, [bool]$withCourse) {
        if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
        $archive = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
        try {
            $prefixLen = $stage.TrimEnd('\').Length + 1
            foreach ($f in Get-ChildItem $stage -Recurse -File) {
                $relative = $f.FullName.Substring($prefixLen)
                if (-not $withCourse -and $relative -like 'course\*') { continue }
                $entry = "$appName/" + $relative.Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $f.FullName, $entry,
                    [System.IO.Compression.CompressionLevel]::Optimal)
            }
        } finally {
            $archive.Dispose()
        }
        Write-Host ("    {0}  {1:N1} MB" -f (Split-Path $zipPath -Leaf),
                                            ((Get-Item $zipPath).Length / 1MB))
    }

    # The shareable one never contains course material, whatever is staged.
    Step "Zipping"
    New-Package (Join-Path $OutDir "$appName-$Version-win64.zip") $false

    # And a second one with it, for your own machines.
    if ($coursePgns.Count -gt 0) {
        New-Package (Join-Path $OutDir "$appName-$Version-win64-with-course.zip") $true
        Warn "the -with-course zip carries $($coursePgns.Count) paid PGNs - keep that one off GitHub"
    }
}

Step "Done"
Write-Host "    folder: $stage"
