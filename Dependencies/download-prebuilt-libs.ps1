#!/usr/bin/env pwsh

<#
.SYNOPSIS
Downloads prebuilt IPlug2 dependencies for specified platform.
.DESCRIPTION
This script downloads and unpacks prebuilt IPlug2 dependencies from GitHub Releases.
.PARAMETER Platform
Specifies the target platform: mac, ios, win. If omitted, the script auto-detects the host OS.
.EXAMPLE
.\download-prebuilt-libs.ps1
.\download-prebuilt-libs.ps1 -Platform win
#>

param(
    [string]$Platform
)

# Determine platform if not provided
if (-not $Platform) {
    if ($IsMacOS) {
        $zipFile = 'IPLUG2_DEPS_MAC'
        $folder  = 'mac'
    } elseif ($IsLinux) {
        Write-Error 'Linux is not supported. Exiting.'; exit 1
    } else {
        $zipFile = 'IPLUG2_DEPS_WIN'
        $folder  = 'win'
    }
} else {
    switch ($Platform.ToLower()) {
        'mac' { $zipFile = 'IPLUG2_DEPS_MAC';  $folder = 'mac'; break }
        'ios' { $zipFile = 'IPLUG2_DEPS_IOS';  $folder = 'ios'; break }
        'win' { $zipFile = 'IPLUG2_DEPS_WIN';  $folder = 'win'; break }
        default {
            Write-Error "Invalid platform parameter '$Platform'. Valid values are: mac, ios, win."; exit 1
        }
    }
}

$zipPath = "$zipFile.zip"

Write-Host "Downloading $zipPath..."
Invoke-WebRequest -Uri "https://github.com/iPlug2/iPlug2/releases/download/v1.0.0-beta/$zipFile.zip" -OutFile $zipPath -UseBasicParsing

# Ensure Build directory exists
if (-not (Test-Path -Path 'Build' -PathType Container)) {
    New-Item -ItemType Directory -Path 'Build' | Out-Null
}

# Remove old folders if they exist
$buildFolder = Join-Path -Path 'Build' -ChildPath $folder
if (Test-Path -Path $buildFolder) {
    Remove-Item -Recurse -Force -Path $buildFolder
}

$buildSrc = Join-Path -Path 'Build' -ChildPath 'src'
if (Test-Path -Path $buildSrc) {
    Remove-Item -Recurse -Force -Path $buildSrc
}

Write-Host "Extracting $zipPath..."
Expand-Archive -LiteralPath $zipPath -DestinationPath . -Force

Write-Host "Moving contents to Build directory..."
Move-Item -Path (Join-Path $zipFile '*') -Destination 'Build' -Force

Write-Host "Cleaning up..."
Remove-Item -Recurse -Force -Path $zipFile
Remove-Item -Force -Path '*.zip'

Write-Host 'Done.' 