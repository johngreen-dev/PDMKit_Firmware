#Requires -Version 5.1
param(
    [string]$Port = "COM11",
    [string]$OutputDir = "storage_dump"
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent

if (-not $env:IDF_PATH) {
    . "C:\Users\johng\esp\v5.5\esp-idf\export.ps1"
}

Set-Location $ProjectRoot

$OutputPath = Join-Path $ProjectRoot $OutputDir
New-Item -ItemType Directory -Force -Path $OutputPath | Out-Null

$partTool  = Join-Path $env:IDF_PATH "components\partition_table\parttool.py"
$nvsParser = Join-Path $env:IDF_PATH "components\nvs_flash\nvs_partition_parser\nvs_partition_parser.py"

# --- NVS ---------------------------------------------------------------------
Write-Host ""
Write-Host "=== NVS Partition ===" -ForegroundColor Cyan

$nvsBin = Join-Path $OutputPath "nvs.bin"
python $partTool --port $Port --baud 460800 read_partition `
    --partition-type data --partition-subtype nvs --output $nvsBin

if (Test-Path $nvsBin) {
    if (Test-Path $nvsParser) {
        Write-Host ""
        Write-Host "--- NVS Entries ---" -ForegroundColor Yellow
        python $nvsParser --nvs_input $nvsBin
    } else {
        Write-Warning "NVS parser not found. Raw binary saved to: $nvsBin"
        Write-Host "Expected path: $nvsParser"
    }
}

# --- SPIFFS ------------------------------------------------------------------
Write-Host ""
Write-Host "=== SPIFFS Partition ===" -ForegroundColor Cyan

$spiffsBin = Join-Path $OutputPath "spiffs.bin"

try {
    python $partTool --port $Port --baud 460800 read_partition `
        --partition-type data --partition-subtype spiffs --output $spiffsBin

    Write-Host "SPIFFS binary saved to: $spiffsBin"

    $mkspiffs = Get-Command mkspiffs -ErrorAction SilentlyContinue
    if ($mkspiffs) {
        $spiffsOut = Join-Path $OutputPath "spiffs_files"
        New-Item -ItemType Directory -Force -Path $spiffsOut | Out-Null
        Write-Host ""
        Write-Host "--- SPIFFS Files ---" -ForegroundColor Yellow
        mkspiffs --unpack $spiffsOut $spiffsBin
        Get-ChildItem -Recurse $spiffsOut | Select-Object FullName, Length, LastWriteTime
    } else {
        Write-Host ""
        Write-Host "mkspiffs not in PATH - cannot extract file listing." -ForegroundColor Yellow
        Write-Host "Install it from: https://github.com/igrr/mkspiffs/releases"
        Write-Host "Then run: mkspiffs --unpack .\spiffs_files $spiffsBin"
    }
} catch {
    Write-Host "No SPIFFS partition found (expected if SPIFFS is not in your partition table)." -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "Done. Output in: $OutputPath" -ForegroundColor Green
