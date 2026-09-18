param(
    [string]$RepoRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$readmePath = Join-Path $RepoRoot "README.md"

$sourceDocs = @(
    "docs/01_architecture.md",
    "docs/07_modules_explained.md",
    "docs/02_build_deploy_test.md",
    "docs/03_protocols_and_messages.md",
    "docs/04_hardware_and_flags.md",
    "docs/05_current_message_flow.md",
    "docs/06_rfc_message_queue.md",
    "docs/TODO.md"
)

function Remove-Diacritics {
    param([string]$Text)

    $normalized = $Text.Normalize([Text.NormalizationForm]::FormD)
    $builder = New-Object System.Text.StringBuilder

    foreach ($char in $normalized.ToCharArray()) {
        $category = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($char)
        if ($category -ne [Globalization.UnicodeCategory]::NonSpacingMark) {
            [void]$builder.Append($char)
        }
    }

    return $builder.ToString().Normalize([Text.NormalizationForm]::FormC)
}

function New-Slug {
    param(
        [string]$Title,
        [hashtable]$SlugCounts
    )

    $slug = Remove-Diacritics -Text $Title
    $slug = $slug.ToLowerInvariant()
    $slug = $slug -replace '[`]', ''
    $slug = $slug -replace '[*_~\[\](){}:;,.!?/"]', ''
    $slug = $slug.Replace("'", "")
    $slug = $slug -replace '[^a-z0-9\s-]', ''
    $slug = $slug -replace '\s+', '-'
    $slug = $slug -replace '-+', '-'
    $slug = $slug.Trim("-")

    if ([string]::IsNullOrWhiteSpace($slug)) {
        $slug = "section"
    }

    if ($SlugCounts.ContainsKey($slug)) {
        $SlugCounts[$slug] = [int]$SlugCounts[$slug] + 1
        return "$slug-$($SlugCounts[$slug])"
    }

    $SlugCounts[$slug] = 0
    return $slug
}

function Shift-Heading {
    param([string]$Line)

    if ($Line -notmatch '^(#{1,6})\s+(.+)$') {
        return $Line
    }

    $hashes = $Matches[1]
    $title = $Matches[2]
    $newLevel = [Math]::Min(6, $hashes.Length + 1)

    return (('#' * $newLevel) + ' ' + $title)
}

$bodyLines = New-Object System.Collections.Generic.List[string]

$bodyLines.Add("## Quickstart")
$bodyLines.Add("")
$bodyLines.Add('```bash')
$bodyLines.Add("pip install -r requirements.txt")
$bodyLines.Add("pio run -e esp32-PROD-BLE")
$bodyLines.Add("pio run --target upload -e esp32-PROD-BLE --upload-port COM7")
$bodyLines.Add("pio device monitor -e esp32-PROD-BLE --port COM7")
$bodyLines.Add('```')
$bodyLines.Add("")
$bodyLines.Add("## Documentacion Tecnica Integrada")
$bodyLines.Add("")

foreach ($relativeDoc in $sourceDocs) {
    $fullPath = Join-Path $RepoRoot $relativeDoc

    if (-not (Test-Path $fullPath)) {
        throw "No se encontro el archivo fuente: $relativeDoc"
    }

    $bodyLines.Add("<!-- BEGIN: $relativeDoc -->")

    foreach ($line in [System.IO.File]::ReadAllLines($fullPath)) {
        $bodyLines.Add((Shift-Heading -Line $line))
    }

    $bodyLines.Add("<!-- END: $relativeDoc -->")
    $bodyLines.Add("")
}

$bodyLines.Add("## Equipo y contribuyentes")
$bodyLines.Add("")
$bodyLines.Add("[![Avatar de @omsmarian](https://github.com/omsmarian.png?size=120)](https://github.com/omsmarian)\\")
$bodyLines.Add("[@omsmarian](https://github.com/omsmarian)")
$bodyLines.Add("")

$slugCounts = @{}
$tocLines = New-Object System.Collections.Generic.List[string]

foreach ($line in $bodyLines) {
    if ($line -match "^(##|###)\s+(.+)$") {
        $level = $Matches[1].Length
        $title = $Matches[2].Trim()
        $slug = New-Slug -Title $title -SlugCounts $slugCounts

        if ($level -eq 2) {
            $tocLines.Add("- [$title](#$slug)")
        } elseif ($level -eq 3) {
            $tocLines.Add("  - [$title](#$slug)")
        }
    }
}

$headerLines = New-Object System.Collections.Generic.List[string]
$headerLines.Add('<p align="center">')
$headerLines.Add('  <img src="docs/AgX%20Logo.jpeg" alt="AGX" width="220">')
$headerLines.Add('</p>')
$headerLines.Add("")
$headerLines.Add("# AGX Compact ESP32")
$headerLines.Add("")
$headerLines.Add("Firmware para ESP32 orientado a adquisicion de datos y telemetria, con integracion de sensores, CAN (J1939/ISOBUS/VG55R), puentes BLE/WiFi y OTA por BLE.")
$headerLines.Add("")
$headerLines.Add("[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-F58220?style=flat-square&logo=platformio&logoColor=white)](platformio.ini) [![Framework](https://img.shields.io/badge/Framework-Arduino-00979D?style=flat-square&logo=arduino&logoColor=white)](platformio.ini) [![WiFi](https://img.shields.io/badge/WiFi-Enabled-2563EB?style=flat-square)](docs/02_build_deploy_test.md) [![BLE](https://img.shields.io/badge/BLE-Enabled-0EA5E9?style=flat-square&logo=bluetooth&logoColor=white)](docs/02_build_deploy_test.md) [![UDP](https://img.shields.io/badge/UDP-TRAX-1D4ED8?style=flat-square)](docs/03_protocols_and_messages.md) [![CAN](https://img.shields.io/badge/CAN-Enabled-4B5563?style=flat-square)](docs/03_protocols_and_messages.md) [![ISOBUS/J1939](https://img.shields.io/badge/ISOBUS%2FJ1939-Supported-005A9C?style=flat-square)](docs/03_protocols_and_messages.md) [![Docs](https://img.shields.io/badge/Docs-Available-6B7280?style=flat-square)](docs/README.md) [![Modular](https://img.shields.io/badge/Modular-Yes-0284C7?style=flat-square)](docs/README.md)")
$headerLines.Add("")
$headerLines.Add("## Tabla de contenidos")
$headerLines.Add("")

foreach ($toc in $tocLines) {
    $headerLines.Add($toc)
}

$headerLines.Add("")
$headerLines.Add("> [!IMPORTANT]")
$headerLines.Add('> Este README se genera automaticamente desde documentos modulares en `docs/`. No editar manualmente secciones integradas.')
$headerLines.Add('> Regenerar: `powershell -ExecutionPolicy Bypass -File scripts/build_readme.ps1`.')
$headerLines.Add("")

$finalLines = New-Object System.Collections.Generic.List[string]
foreach ($line in $headerLines) { $finalLines.Add($line) }
foreach ($line in $bodyLines) { $finalLines.Add($line) }

$finalText = ($finalLines -join "`r`n") + "`r`n"
[System.IO.File]::WriteAllText($readmePath, $finalText, [System.Text.UTF8Encoding]::new($false))

Write-Output "README generado en: $readmePath"