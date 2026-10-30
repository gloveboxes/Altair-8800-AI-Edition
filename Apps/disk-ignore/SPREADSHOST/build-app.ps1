#!/usr/bin/env pwsh

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path
& (Join-Path $repoRoot 'scripts' 'build-dcc-app.ps1') -AppDirectory $PSScriptRoot