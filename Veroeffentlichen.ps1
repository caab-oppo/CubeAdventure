# Veroeffentlichen.ps1 -- baut aus der Testinstallation das Paket zum Hochladen.
#
#   Projekt\Veroeffentlichung\CubeAdventure_<Datum>\        (Ordner zum Ansehen)
#   Projekt\Veroeffentlichung\CubeAdventure_<Datum>.zip     (zum Hochladen)
#
# Inhalt (genau das, was der Spieler in seinen Cube-World-Ordner kopiert):
#   Mods\AdventureMod.dll       aus <Spielordner>\Mods (Release bauen nicht vergessen!)
#   Strukturen\                 der EINE Strukturen-Ordner, ohne weltbezogene und Arbeitsdateien
#   LIESMICH.txt, Lizenzen\     aus Projekt\Doku
#
# Aufruf: Rechtsklick -> "Mit PowerShell ausfuehren"  oder  Veroeffentlichen.bat
$ErrorActionPreference = 'Stop'
$projekt = $PSScriptRoot
$spiel   = Split-Path $projekt -Parent
$datum   = Get-Date -Format 'yyyy-MM-dd'
$name    = "CubeAdventure_$datum"
$ziel    = Join-Path $projekt "Veroeffentlichung\$name"
$zip     = "$ziel.zip"

$dll = Join-Path $spiel 'Mods\AdventureMod.dll'
if (-not (Test-Path $dll)) { throw "Keine Mods\AdventureMod.dll im Spielordner -- erst Release|Win32 bauen." }

if (Test-Path $ziel) { Remove-Item $ziel -Recurse -Force }
if (Test-Path $zip)  { Remove-Item $zip -Force }
New-Item -ItemType Directory -Path (Join-Path $ziel 'Mods') -Force | Out-Null

# --- Sicherheitspruefung der DLL (2026-10-29): keine persoenlichen Pfade, kein Debug-Build ---
# Eine Debug-DLL enthaelt den vollen PDB-Pfad (C:\Users\<Name>\...) und Include-Pfade; Release setzt /PDBALTPATH.
$bytes = [System.IO.File]::ReadAllBytes($dll)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
$utf16 = [System.Text.Encoding]::Unicode.GetString($bytes)
$verboten = @("\Users\$env:USERNAME", "\Users\", "\Desktop\", "\Downloads\", "\Debug\AdventureMod.pdb",
              'MSVCP140D.dll', 'VCRUNTIME140D.dll', 'ucrtbased.dll', 'd3dx9_43.dll')
foreach ($v in $verboten) {
    if ($ascii.IndexOf($v, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $utf16.IndexOf($v, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "AdventureMod.dll enthaelt '$v' -- bitte Release|Win32 bauen (kein Debug) und erneut starten."
    }
}
Copy-Item $dll (Join-Path $ziel 'Mods\AdventureMod.dll')

# Strukturen: alles ausser weltbezogenen Daten und Arbeitsdateien
$src = Join-Path $spiel 'Strukturen'
$ausschluss = @(
    '\\_autosave\\',            # LAB-Autosicherung
    '\\Export\\',               # exportierte native Modelle (Spieldaten!)
    '\\Mobs\\Vorschau\\',       # Vorschaubilder
    '\\platzierungen_[0-9A-Fa-f]+\.txt$',   # gemerkte LAB-Platzierungen einer Welt
    '\\Biome\\_erzwungen\.txt$',            # erzwungene Biomzellen einer Welt
    '\.py$',                                # Skripte
    '\.(exe|dll|pdb)$',                     # Programme gehoeren nie in Strukturen (z. B. versehentlich kopierte Starter)
    '\\Shader\\',                         # Shader (2026-10-29 aus dem Mod entfernt)
    '\\LabApi\\',                         # Datei-API des LAB (nur Entwicklung)
    '\.orig\.cub$',                         # Sicherungen der Bein-Modelle
    '\.(alt|bak|tmp|log|erledigt)$'         # Alt-/Arbeitsdateien
)
Get-ChildItem $src -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($src.Length)
    foreach ($m in $ausschluss) { if ($rel -match $m) { return } }
    $dst = Join-Path (Join-Path $ziel 'Strukturen') $rel
    New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
    Copy-Item $_.FullName $dst
}

Copy-Item (Join-Path $projekt 'Doku\LIESMICH.txt') (Join-Path $ziel 'LIESMICH.txt')
$liz = Join-Path $projekt 'Doku\Lizenzen'
if (Test-Path $liz) { Copy-Item $liz (Join-Path $ziel 'Lizenzen') -Recurse }

# --- Sicherheitspruefung der Textdateien: keine Benutzernamen, Pfade, E-Mail-Adressen oder Schluessel ---
$muster = @("(?i)\\Users\\", "(?i)$([regex]::Escape($env:USERNAME))", '(?i)[A-Z]:\\', '[\w.+-]+@[\w-]+\.[\w.]+',
            '(?i)(api[_-]?key|secret|password|passwort|bearer|token\s*=)', 'sk-[A-Za-z0-9]{16,}')
$funde = @()
Get-ChildItem $ziel -Recurse -File -Include *.txt,*.cfg,*.mob,*.md,*.json,*.ini,*.skill,*.biom,*.tiere | ForEach-Object {
    $t = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($t) { foreach ($m in $muster) { if ($t -match $m) { $funde += "$($_.FullName.Substring($ziel.Length)) : $($Matches[0])" } } }
}
if ($funde.Count -gt 0) {
    $funde | ForEach-Object { Write-Host "  PRUEFEN: $_" -ForegroundColor Yellow }
    throw "Moegliche persoenliche Daten im Paket (siehe oben) -- Dateien bereinigen und erneut starten."
}

Compress-Archive -Path (Join-Path $ziel '*') -DestinationPath $zip
$n = (Get-ChildItem $ziel -Recurse -File).Count
Write-Host "Fertig: $zip  ($n Dateien)"
Write-Host "Vor dem Hochladen: Projekt\Doku\VEROEFFENTLICHUNG.md (Rechte/Lizenzen, Testliste)."
