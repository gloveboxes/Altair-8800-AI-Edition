#!/usr/bin/env pwsh

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$appsRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildScripts = @(
	Get-ChildItem -LiteralPath $appsRoot -Directory |
		ForEach-Object { Join-Path $_.FullName 'build-app.ps1' } |
		Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
		Sort-Object
)
if (-not $buildScripts) {
	throw "No build-app.ps1 scripts found under $appsRoot"
}

$totalTimer = [Diagnostics.Stopwatch]::StartNew()
Write-Host "Building $($buildScripts.Count) dcc applications..."

for ($index = 0; $index -lt $buildScripts.Count; $index++) {
	$buildScript = $buildScripts[$index]
	$appName = Split-Path (Split-Path $buildScript -Parent) -Leaf
	$appTimer = [Diagnostics.Stopwatch]::StartNew()
	Write-Host "[$($index + 1)/$($buildScripts.Count)] $appName"
	& $buildScript
	$appTimer.Stop()
	Write-Host "[$($index + 1)/$($buildScripts.Count)] $appName PASS ($($appTimer.ElapsedMilliseconds) ms)"
}

$totalTimer.Stop()
Write-Host "BUILD ALL PASS: $($buildScripts.Count) applications ($($totalTimer.ElapsedMilliseconds) ms)"