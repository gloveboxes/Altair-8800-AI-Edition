#!/usr/bin/env pwsh

param(
	[switch] $AssemblyOnly,

	[string] $DccDirectory,

	[string] $Ntvcm = 'ntvcm'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$appsRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildScripts = @(
	Get-ChildItem -LiteralPath $appsRoot -Directory |
		ForEach-Object { Join-Path $_.FullName 'build-app.ps1' } |
		Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
		Sort-Object
)
if (-not $buildScripts -and -not $AssemblyOnly) {
	throw "No build-app.ps1 scripts found under $appsRoot"
}
if ($AssemblyOnly) {
	$buildScripts = @()
}
$assemblySources = @(
	Get-ChildItem -LiteralPath $appsRoot -Directory |
		ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -File -Filter '*.ASM' } |
		Where-Object {
			$existingBuild = Join-Path $_.DirectoryName 'build-app.ps1'
			-not (Test-Path -LiteralPath $existingBuild -PathType Leaf) -or
				$_.BaseName -ine $_.Directory.Name
		} |
		Sort-Object FullName
)
$assemblyBuilder = Join-Path $appsRoot '..' 'scripts' 'build-asm-app.ps1'
$totalCount = $buildScripts.Count + $assemblySources.Count

$totalTimer = [Diagnostics.Stopwatch]::StartNew()
Write-Host "Building $($buildScripts.Count) dcc and $($assemblySources.Count) assembly applications..."

for ($index = 0; $index -lt $buildScripts.Count; $index++) {
	$buildScript = $buildScripts[$index]
	$appName = Split-Path (Split-Path $buildScript -Parent) -Leaf
	$appTimer = [Diagnostics.Stopwatch]::StartNew()
	Write-Host "[$($index + 1)/$totalCount] $appName (dcc)"
	& $buildScript
	$appTimer.Stop()
	Write-Host "[$($index + 1)/$totalCount] $appName PASS ($($appTimer.ElapsedMilliseconds) ms)"
}

for ($index = 0; $index -lt $assemblySources.Count; $index++) {
	$source = $assemblySources[$index]
	$number = $buildScripts.Count + $index + 1
	$appTimer = [Diagnostics.Stopwatch]::StartNew()
	Write-Host "[$number/$totalCount] $($source.BaseName) (assembly)"
	& $assemblyBuilder -Source $source.FullName -DccDirectory $DccDirectory -Ntvcm $Ntvcm
	$appTimer.Stop()
	Write-Host "[$number/$totalCount] $($source.BaseName) PASS ($($appTimer.ElapsedMilliseconds) ms)"
}

$totalTimer.Stop()
Write-Host "BUILD ALL PASS: $totalCount applications ($($totalTimer.ElapsedMilliseconds) ms)"