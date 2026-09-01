#!/usr/bin/env pwsh

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-DccSetting {
	param(
		[Parameter(Mandatory)]
		[string] $Path,

		[Parameter(Mandatory)]
		[string] $Name
	)

	$pattern = '^\s*' + [regex]::Escape($Name) + '\s*=\s*([^#\s]+)'
	$values = @(foreach ($line in Get-Content -LiteralPath $Path) {
		if ($line -match $pattern) {
			$Matches[1]
		}
	})
	if (-not $values) {
		throw "$Name is not set in $Path"
	}
	$values[-1]
}

function Invoke-NativeCommand {
	param(
		[Parameter(Mandatory)]
		[string] $Command,

		[string[]] $Arguments = @()
	)

	& $Command @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Command failed with exit code $LASTEXITCODE"
	}
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path
$configPath = Join-Path $PSScriptRoot 'dccmake.txt'
$outputFile = "$(Get-DccSetting -Path $configPath -Name 'dcc-output').COM"
$buildDirectory = Join-Path $PSScriptRoot 'build'

Push-Location $PSScriptRoot
try {
	Remove-Item -LiteralPath $buildDirectory -Recurse -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $outputFile -Force -ErrorAction SilentlyContinue
	Invoke-NativeCommand -Command 'dccmake'
	Move-Item -LiteralPath (Join-Path $buildDirectory $outputFile) -Destination $PSScriptRoot -Force
	Remove-Item -LiteralPath $buildDirectory -Recurse -Force

	& (Join-Path $repoRoot 'scripts' 'cpm-install.ps1') `
		-Image (Join-Path $repoRoot 'disks' 'escape-posix.dsk') `
		-HostFile (Join-Path $PSScriptRoot $outputFile) `
		-CpmFile "0:$outputFile"
}
finally {
	Pop-Location
}