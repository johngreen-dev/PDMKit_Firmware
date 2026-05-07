#Requires -Version 5.1
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent

if (-not $env:IDF_PATH) {
    . "C:\Users\johng\esp\v5.5\esp-idf\export.ps1"
}

Set-Location $ProjectRoot
idf.py build
