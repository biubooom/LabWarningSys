$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$logPath = Join-Path $scriptDir 'cubemx_post_generate.log'

function Write-Log {
    param([string]$Message)

    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    [System.IO.File]::AppendAllText($logPath, "[$timestamp] $Message`r`n")
}

function Disable-HandlerDefinition {
    param(
        [string]$Content,
        [string]$HandlerName
    )

    $signaturePattern = "(?m)^(?<indent>\s*)(?!//\s*)void\s+$HandlerName\s*\(\s*void\s*\)"
    $match = [regex]::Match($Content, $signaturePattern)

    if (-not $match.Success) {
        return @{
            Content = $Content
            Changed = $false
        }
    }

    $braceStart = $Content.IndexOf('{', $match.Index + $match.Length)
    if ($braceStart -lt 0) {
        return @{
            Content = $Content
            Changed = $false
        }
    }

    $depth = 0
    $braceEnd = -1
    for ($i = $braceStart; $i -lt $Content.Length; $i++) {
        if ($Content[$i] -eq '{') {
            $depth++
        }
        elseif ($Content[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) {
                $braceEnd = $i
                break
            }
        }
    }

    if ($braceEnd -lt 0) {
        return @{
            Content = $Content
            Changed = $false
        }
    }

    $lineStart = $Content.LastIndexOf("`n", $match.Index)
    if ($lineStart -lt 0) {
        $lineStart = 0
    }
    else {
        $lineStart++
    }

    $lineEnd = $Content.IndexOf("`n", $braceEnd)
    if ($lineEnd -lt 0) {
        $lineEnd = $Content.Length
    }
    else {
        $lineEnd++
    }

    $block = $Content.Substring($lineStart, $lineEnd - $lineStart).TrimEnd("`r", "`n")
    $indent = $match.Groups['indent'].Value
    $commented = ($block -split "\r?\n" | ForEach-Object { "$indent// $_" }) -join "`r`n"
    $newContent = $Content.Remove($lineStart, $lineEnd - $lineStart).Insert($lineStart, $commented + "`r`n")

    return @{
        Content = $newContent
        Changed = $true
    }
}

function Disable-HandlerDeclaration {
    param(
        [string]$Content,
        [string]$HandlerName
    )

    $pattern = "(?m)^(?<indent>\s*)(?!//\s*)void\s+$HandlerName\s*\(\s*void\s*\)\s*;\s*$"
    $match = [regex]::Match($Content, $pattern)

    if (-not $match.Success) {
        return @{
            Content = $Content
            Changed = $false
        }
    }

    $replacement = "$($match.Groups['indent'].Value)// void $HandlerName(void);"
    return @{
        Content = $Content.Remove($match.Index, $match.Length).Insert($match.Index, $replacement)
        Changed = $true
    }
}

$itSourcePath = Join-Path $projectRoot 'Core\Src\stm32f1xx_it.c'
$itHeaderPath = Join-Path $projectRoot 'Core\Inc\stm32f1xx_it.h'

if (-not (Test-Path $itSourcePath)) {
    throw "Missing file: $itSourcePath"
}

if (-not (Test-Path $itHeaderPath)) {
    throw "Missing file: $itHeaderPath"
}

$sourceContent = Get-Content $itSourcePath -Raw
$headerContent = Get-Content $itHeaderPath -Raw

$sourceChanged = $false
$headerChanged = $false

foreach ($handler in @('SVC_Handler', 'PendSV_Handler')) {
    $sourceResult = Disable-HandlerDefinition -Content $sourceContent -HandlerName $handler
    $sourceContent = $sourceResult.Content
    $sourceChanged = $sourceChanged -or $sourceResult.Changed

    $headerResult = Disable-HandlerDeclaration -Content $headerContent -HandlerName $handler
    $headerContent = $headerResult.Content
    $headerChanged = $headerChanged -or $headerResult.Changed
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($itSourcePath, $sourceContent, $utf8NoBom)
[System.IO.File]::WriteAllText($itHeaderPath, $headerContent, $utf8NoBom)

Write-Log "patched source=$sourceChanged header=$headerChanged"
Write-Host 'CubeMX post-generate patch applied: SVC_Handler, PendSV_Handler'
