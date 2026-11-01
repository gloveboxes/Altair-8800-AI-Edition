#!/usr/bin/env pwsh
param([switch] $BuildOnly)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..' '..')).Path
& (Join-Path $repoRoot 'scripts' 'build-dcc-app.ps1') `
    -AppDirectory $PSScriptRoot -BuildOnly:$BuildOnly
