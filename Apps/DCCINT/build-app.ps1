#!/usr/bin/env pwsh

if ([string]::IsNullOrWhiteSpace($env:DCC_DIR)) {
	throw 'Set DCC_DIR to the dcc repository path'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path
$dccmake = Join-Path $env:DCC_DIR 'dccmake'
& (Join-Path $repoRoot 'scripts' 'build-dcc-app.ps1') `
	-AppDirectory $PSScriptRoot `
	-Dccmake $dccmake