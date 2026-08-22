<#
.SYNOPSIS
    Splits a large multi-game PGN into one file per chapter, so a collection
    browses as themed chapters instead of one endless list of positions.

.DESCRIPTION
    Course exports of books tend to arrive as a single file with every position
    in it, tagged only "Chapter 7 (2)". The trainer can read that, but a chapter
    with several thousand lines is not something anyone can navigate.

    This groups the games by chapter number and writes one PGN per chapter,
    named from a lookup table so the files say what they are about. Within a
    chapter the positions are renumbered in one run, so "Position 57" means the
    same thing wherever you are in the file.

    Games are copied verbatim apart from the Event/Site/White/Black tags; the
    FEN, the moves and the annotations are untouched.

.PARAMETER Path
    The multi-game PGN to split.

.PARAMETER OutDir
    Where to write the chapters. Defaults to "<name> - By Chapter" beside the
    source file.

.PARAMETER Names
    Optional text file of "number = title" lines giving each chapter a name.
    Lines starting with # are ignored. Chapters with no entry keep the number.

.PARAMETER Prefix
    Put in front of every file name. Worth setting when the chapters will share
    a folder with another book, so the two do not interleave in the list -
    "DEM " turns "01 - Pawn Endgames.pgn" into "DEM 01 - Pawn Endgames.pgn".

.EXAMPLE
    .\split_pgn_by_chapter.ps1 -Path "Dvoretsky's Endgame Manual - 5th Edition.pgn" `
                               -Names .\dvoretsky-chapters.txt
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [string]$OutDir,
    [string]$Names,
    [string]$Prefix = "",
    [ValidateSet("Event","White","Black","Site")][string]$GroupBy,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $Path)) { throw "PGN not found: $Path" }
$source = Get-Item -LiteralPath $Path

if (-not $OutDir) {
    $OutDir = Join-Path $source.DirectoryName ($source.BaseName + " - By Chapter")
}

# --- chapter titles ---------------------------------------------------------

$titles = @{}
if ($Names) {
    if (-not (Test-Path -LiteralPath $Names)) { throw "Names file not found: $Names" }
    foreach ($line in Get-Content -LiteralPath $Names) {
        $t = $line.Trim()
        if (-not $t -or $t.StartsWith("#")) { continue }
        $eq = $t.IndexOf("=")
        if ($eq -lt 1) { continue }
        $titles[$t.Substring(0, $eq).Trim()] = $t.Substring($eq + 1).Trim()
    }
    Write-Host "Loaded $($titles.Count) chapter titles"
}

# --- read the games ---------------------------------------------------------

Write-Host "Reading $($source.Name)"
$lines = [System.IO.File]::ReadAllLines($source.FullName)

# A game is a header block plus its movetext. A tag line seen after movetext
# has begun is the start of the next game.
$games = New-Object System.Collections.ArrayList
$headers = New-Object System.Collections.ArrayList
$moves = New-Object System.Collections.ArrayList
$inMoves = $false

function Get-Tag([object[]]$block, [string]$name) {
    foreach ($h in $block) {
        if ($h -match ('^\s*\[' + $name + '\s+"(.*)"\s*\]')) { return $Matches[1] }
    }
    return ""
}

function Save-Game {
    if ($headers.Count -eq 0 -and $moves.Count -eq 0) { return }
    $block = @($headers.ToArray())
    [void]$games.Add([pscustomobject]@{
        Event   = (Get-Tag $block "Event")
        White   = (Get-Tag $block "White")
        Black   = (Get-Tag $block "Black")
        Site    = (Get-Tag $block "Site")
        Headers = $block
        Moves   = @($moves.ToArray())
    })
    $headers.Clear(); $moves.Clear()
}

foreach ($line in $lines) {
    $t = $line.Trim()
    $isTag = $t -match '^\[[A-Za-z0-9_]+\s+".*"\s*\]$'
    if ($isTag) {
        if ($inMoves) { Save-Game; $inMoves = $false }
        [void]$headers.Add($line)
        continue
    }
    if (-not $t -and -not $inMoves) { continue }
    $inMoves = $true
    [void]$moves.Add($line)
}
Save-Game

Write-Host "Found $($games.Count) games"
if ($games.Count -eq 0) { throw "No games found - is this a PGN?" }

# --- work out what a chapter is in this file --------------------------------

# Exports do not agree. One tags every game "Chapter 7 (2)" in Event; another
# leaves Event as "?" and puts the chapter name in White. Pick whichever tag
# actually distinguishes the games.
function Measure-Distinct([string]$tag) {
    return @($games | ForEach-Object { $_.$tag } |
             Where-Object { $_ -and $_ -ne "?" } | Sort-Object -Unique).Count
}
if (-not $GroupBy) {
    $GroupBy = if ((Measure-Distinct "Event") -gt 1) { "Event" }
               elseif ((Measure-Distinct "White") -gt 1) { "White" }
               else { "Event" }
}
Write-Host "Grouping by the $GroupBy tag ($(Measure-Distinct $GroupBy) distinct values)"

foreach ($g in $games) {
    $key = $g.$GroupBy
    $section = ""
    # "Chapter 7 (2)" is chapter 7, section 2; anything else is its own chapter.
    if ($key -match '^\s*Chapter\s+(\d+)\s*(?:\((\d+)\))?') {
        $key = $Matches[1]
        if ($Matches[2]) { $section = $Matches[2] }
    }
    Add-Member -InputObject $g -NotePropertyName Chapter -NotePropertyValue $key -Force
    Add-Member -InputObject $g -NotePropertyName Section -NotePropertyValue $section -Force
}

# --- group and write --------------------------------------------------------

# Keep the order the chapters appear in, and sort numerically where we can.
$order = @()
foreach ($g in $games) { if ($order -notcontains $g.Chapter) { $order += $g.Chapter } }
$numeric = @($order | Where-Object { $_ -match '^\d+$' } | Sort-Object { [int]$_ })
$other   = @($order | Where-Object { $_ -notmatch '^\d+$' })
$order   = @($numeric) + @($other)

if (Test-Path -LiteralPath $OutDir) {
    if (-not $Force) {
        $existing = @(Get-ChildItem -LiteralPath $OutDir -Filter *.pgn -ErrorAction SilentlyContinue)
        if ($existing.Count -gt 0) {
            throw "$OutDir already holds $($existing.Count) PGN files - pass -Force to replace them."
        }
    } else {
        Get-ChildItem -LiteralPath $OutDir -Filter *.pgn -ErrorAction SilentlyContinue |
            Remove-Item -Force
    }
} else {
    New-Item -ItemType Directory -Force $OutDir | Out-Null
}

# Windows will not accept these in a file name.
function ConvertTo-SafeName([string]$s) {
    foreach ($c in [System.IO.Path]::GetInvalidFileNameChars()) { $s = $s.Replace($c, '-') }
    return $s.Trim()
}

$index = 0
$totalWritten = 0
foreach ($chapter in $order) {
    $index++
    # A numeric key needs a name from the table; a key that is already a chapter
    # name ("4. Noteboom - Main Line") is its own title.
    $title = if     ($titles.ContainsKey($chapter)) { $titles[$chapter] }
             elseif ($chapter -match '^\d+$')       { "Chapter $chapter" }
             else                                    { $chapter }
    $file = Join-Path $OutDir (ConvertTo-SafeName ("{0}{1:d2} - {2}.pgn" -f $Prefix, $index, $title))

    $chapterGames = @($games | Where-Object { $_.Chapter -eq $chapter })
    $out = New-Object System.Text.StringBuilder
    $position = 0

    foreach ($g in $chapterGames) {
        $position++
        $eventName = if ($g.Section) { "$title ($($g.Section))" } else { $title }

        # What to call this position. A repertoire names its lines in the Black
        # tag and that is far more use than a number; a manual just writes
        # "Game 12" there, which restarts every chapter, so number it instead.
        $siteName = if ($g.Black -and $g.Black -ne "?" -and $g.Black -notmatch '^Game\s+\d+$') {
            $g.Black
        } else {
            "Position $position"
        }

        foreach ($h in $g.Headers) {
            if     ($h -match '^\s*\[Event\s') { [void]$out.AppendLine("[Event `"$eventName`"]") }
            elseif ($h -match '^\s*\[Site\s')  { [void]$out.AppendLine("[Site `"$siteName`"]") }
            elseif ($h -match '^\s*\[White\s') { [void]$out.AppendLine("[White `"$eventName`"]") }
            elseif ($h -match '^\s*\[Black\s') { [void]$out.AppendLine("[Black `"$siteName`"]") }
            else                               { [void]$out.AppendLine($h) }
        }
        [void]$out.AppendLine("")
        foreach ($m in $g.Moves) { [void]$out.AppendLine($m) }
        [void]$out.AppendLine("")
    }

    # UTF-8 without a BOM: the trainer reads these as UTF-8 and a BOM would
    # end up inside the first tag.
    $enc = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($file, $out.ToString(), $enc)

    $totalWritten += $chapterGames.Count
    "{0,-52} {1,5} positions" -f (Split-Path $file -Leaf), $chapterGames.Count
}

Write-Host ""
Write-Host "Wrote $index chapters / $totalWritten positions to $OutDir"
