#requires -Version 5.1
<#
    build.ps1  --  Build WandCursor.exe with MinGW g++.

    Usage:
        .\build.ps1

    Requires MinGW-w64 (https://www.mingw-w64.org/). The script looks for
    g++ at C:\mingw64\bin\g++.exe first, then falls back to g++ on PATH.

#>

$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot 'WandCursor.c'
$out = Join-Path $PSScriptRoot 'WandCursor.exe'

# Locate the compiler: prefer a standard MinGW-w64 install, then PATH.
$gxx = 'C:\mingw64\bin\g++.exe'
if (-not (Test-Path $gxx)) {
    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($cmd) {
        $gxx = $cmd.Source
    } else {
        Write-Error "g++ not found. Install MinGW-w64 (https://www.mingw-w64.org/) or add g++ to PATH."
    }
}

Write-Host "Compiler : $gxx"
Write-Host "Source   : $src"
Write-Host "Output   : $out"
Write-Host ''

& $gxx $src -o $out `
    -municode -mwindows `
    -static -static-libgcc -static-libstdc++ `
    -lgdiplus -lgdi32 -luser32 -O2 -Wall

if ($LASTEXITCODE -eq 0) {
    $kb = [math]::Round((Get-Item $out).Length / 1KB, 1)
    Write-Host ''
    Write-Host "Build OK -> $out ($kb KB)" -ForegroundColor Green
} else {
    Write-Error "Build failed (g++ exit code $LASTEXITCODE)."
}
