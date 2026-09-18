param(
    [string]$TodoPath = "docs/TODO.md",
    [string]$Owner = "",
    [string]$Repo = "",
    [string]$Token = "",
    [switch]$Execute,
    [int]$Limit = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-OwnerRepoFromOrigin {
    $origin = (git remote get-url origin).Trim()

    if ($origin -match '^git@github\.com:(?<owner>[^/]+)/(?<repo>[^\.]+)(\.git)?$') {
        return @{ owner = $Matches['owner']; repo = $Matches['repo'] }
    }

    if ($origin -match '^https://github\.com/(?<owner>[^/]+)/(?<repo>[^\.]+)(\.git)?$') {
        return @{ owner = $Matches['owner']; repo = $Matches['repo'] }
    }

    throw "No se pudo parsear origin remoto: $origin"
}

function Parse-TodoIssues {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "No existe archivo TODO: $Path"
    }

    $lines = [System.IO.File]::ReadAllLines($Path)
    $section = "General"
    $issues = New-Object System.Collections.Generic.List[object]

    $sectionLabelMap = @{
        "Documentacion" = "area:docs"
        "Arquitectura de mensajeria" = "area:architecture"
        "Confiabilidad y telemetria" = "area:reliability"
        "Testing" = "area:testing"
    }

    $priorityLabelMap = @{
        "Alta" = "priority:high"
        "Media" = "priority:medium"
        "Baja" = "priority:low"
    }

    foreach ($line in $lines) {
        if ($line -match '^##\s+\d+\.\s+(.+)$') {
            $section = $Matches[1].Trim()
            continue
        }

        if ($line -match '^- \[ \] \[(Alta|Media|Baja)\] (.+)$') {
            $priority = $Matches[1].Trim()
            $text = $Matches[2].Trim()

            $sectionTag = $section
            $title = "[$sectionTag] $text"

            $areaLabel = if ($sectionLabelMap.ContainsKey($section)) { $sectionLabelMap[$section] } else { "area:general" }
            $priorityLabel = $priorityLabelMap[$priority]

            $body = @(
                "## Contexto",
                "Issue generado automaticamente desde docs/TODO.md.",
                "",
                "## Detalle del TODO",
                "- Seccion: $section",
                "- Prioridad: $priority",
                "- Tarea: $text",
                "",
                "## Criterio de cierre",
                "- [ ] Implementacion finalizada",
                "- [ ] Documentacion actualizada",
                "- [ ] Validacion tecnica realizada",
                "",
                "Fuente: docs/TODO.md"
            ) -join "`n"

            $issues.Add([pscustomobject]@{
                title = $title
                body = $body
                labels = @("type:todo", $areaLabel, $priorityLabel)
            })
        }
    }

    return $issues
}

function Ensure-Labels {
    param(
        [string]$Owner,
        [string]$Repo,
        [hashtable]$Headers
    )

    $labels = @(
        @{ name = "type:todo"; color = "0E8A16"; description = "Issue generado desde backlog TODO" },
        @{ name = "area:docs"; color = "1D76DB"; description = "Documentacion" },
        @{ name = "area:architecture"; color = "5319E7"; description = "Arquitectura de mensajeria" },
        @{ name = "area:reliability"; color = "D93F0B"; description = "Confiabilidad y telemetria" },
        @{ name = "area:testing"; color = "FBCA04"; description = "Testing y QA" },
        @{ name = "area:general"; color = "BFD4F2"; description = "General" },
        @{ name = "priority:high"; color = "B60205"; description = "Prioridad alta" },
        @{ name = "priority:medium"; color = "FBCA04"; description = "Prioridad media" },
        @{ name = "priority:low"; color = "0E8A16"; description = "Prioridad baja" }
    )

    foreach ($label in $labels) {
        $url = "https://api.github.com/repos/$Owner/$Repo/labels"
        try {
            Invoke-RestMethod -Method Post -Uri $url -Headers $Headers -Body ($label | ConvertTo-Json -Depth 4) | Out-Null
            Write-Host "[label] creado: $($label.name)"
        }
        catch {
            if ($_.Exception.Message -match "422") {
                Write-Host "[label] existe: $($label.name)"
            }
            else {
                throw
            }
        }
    }
}

$repoRoot = (Get-Location).Path

if ([string]::IsNullOrWhiteSpace($Owner) -or [string]::IsNullOrWhiteSpace($Repo)) {
    $parsed = Get-OwnerRepoFromOrigin
    if ([string]::IsNullOrWhiteSpace($Owner)) { $Owner = $parsed.owner }
    if ([string]::IsNullOrWhiteSpace($Repo)) { $Repo = $parsed.repo }
}

$issues = Parse-TodoIssues -Path (Join-Path $repoRoot $TodoPath)

if ($Limit -gt 0) {
    $issues = @($issues | Select-Object -First $Limit)
}

Write-Host "Repo destino: $Owner/$Repo"
Write-Host "Issues detectados desde TODO: $($issues.Count)"

if (-not $Execute) {
    Write-Host "Modo preview (sin crear issues). Usa -Execute para crear en GitHub."
    $i = 1
    foreach ($issue in $issues) {
        Write-Host ""
        Write-Host "[$i] $($issue.title)"
        Write-Host "labels: $($issue.labels -join ', ')"
        $i++
    }
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Token)) {
    $Token = $env:GITHUB_TOKEN
}

if ([string]::IsNullOrWhiteSpace($Token)) {
    throw "Falta token. Define -Token o variable GITHUB_TOKEN."
}

$headers = @{
    Authorization = "Bearer $Token"
    Accept = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}

Ensure-Labels -Owner $Owner -Repo $Repo -Headers $headers

$created = New-Object System.Collections.Generic.List[object]

foreach ($issue in $issues) {
    $url = "https://api.github.com/repos/$Owner/$Repo/issues"
    $payload = @{
        title = $issue.title
        body = $issue.body
        labels = $issue.labels
    }

    $resp = Invoke-RestMethod -Method Post -Uri $url -Headers $headers -Body ($payload | ConvertTo-Json -Depth 8)
    $created.Add([pscustomobject]@{ number = $resp.number; title = $resp.title; url = $resp.html_url })
    Write-Host "[issue] #$($resp.number) creado: $($resp.title)"
}

Write-Host ""
Write-Host "Issues creados: $($created.Count)"
foreach ($it in $created) {
    Write-Host "- #$($it.number) $($it.title) -> $($it.url)"
}