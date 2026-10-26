#!/usr/bin/env pwsh

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..' '..')).Path
$arguments = @(
    '-Source', (Join-Path $PSScriptRoot 'LS.ASM'),
    '-DccDirectory', $(if ($env:DCC_DIR) { $env:DCC_DIR } else { (Split-Path (Get-Command dccmake -CommandType Application).Source -Parent) })
)
if ($env:NTVCM_DIR) {
    $arguments += @('-Ntvcm', (Join-Path $env:NTVCM_DIR $(if ($IsWindows) { 'ntvcm.exe' } else { 'ntvcm' })))
}
& (Join-Path $repoRoot 'scripts' 'build-asm-app.ps1') @arguments
