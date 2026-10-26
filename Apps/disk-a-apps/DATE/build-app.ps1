#!/usr/bin/env pwsh

param([switch] $BuildOnly)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..' '..')).Path
$dccDirectory = if ($env:DCC_DIR) { $env:DCC_DIR } else {
    Split-Path (Get-Command dccmake -CommandType Application).Source -Parent
}
& (Join-Path $repoRoot 'scripts' 'build-asm-app.ps1') `
    -Source (Join-Path $PSScriptRoot 'DATE.ASM') `
    -DccDirectory $dccDirectory -BuildOnly:$BuildOnly
