#!/usr/bin/env pwsh

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path
$builder = Join-Path $repoRoot 'scripts' 'build-dcc-app.ps1'
$diskImage = Join-Path $repoRoot 'disks' 'blank.dsk'

& $builder -AppDirectory $PSScriptRoot -DiskImage $diskImage
& $builder `
	-AppDirectory $PSScriptRoot `
	-DiskImage $diskImage `
	-DccArguments @(
		'dcc-input=docgen.c,isamdb.c'
		'dcc-output=DOCGEN'
		'dcc-build-dir=build-docgen'
	)