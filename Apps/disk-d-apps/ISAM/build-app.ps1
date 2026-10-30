#!/usr/bin/env pwsh

param([switch] $BuildOnly)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..' '..')).Path
$builder = Join-Path $repoRoot 'scripts' 'build-dcc-app.ps1'
$diskImage = Join-Path $repoRoot 'disks' 'blank.dsk'

Push-Location $PSScriptRoot
try {
	& dccmake
	if ($LASTEXITCODE -ne 0) { throw "ISAM composite build failed: $LASTEXITCODE" }
	foreach ($project in @('lib', 'doctor', 'docgen')) {
		& $builder -AppDirectory (Join-Path $PSScriptRoot $project) `
			-DiskImage $diskImage -CollectOnly -BuildOnly:$BuildOnly
	}
}
finally {
	Pop-Location
}