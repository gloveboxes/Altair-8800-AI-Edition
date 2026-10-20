#!/usr/bin/env pwsh

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path
$builder = Join-Path $repoRoot 'scripts' 'build-dcc-app.ps1'

& $builder -AppDirectory $PSScriptRoot
& $builder `
	-AppDirectory $PSScriptRoot `
	-DccArguments @(
		'dcc-input=docgen.c,isamdb.c'
		'dcc-output=DOCGEN'
		'dcc-build-dir=build-docgen'
	)