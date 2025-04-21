#!/usr/bin/env pwsh

<#
.SYNOPSIS
Downloads prebuilt IPlug2 dependencies for specified platform.
.DESCRIPTION
This script downloads and unpacks prebuilt IPlug2 dependencies from GitHub Releases.
If needed, it removes specific folders inside the Build directory that would conflict
with the extracted content before moving files.
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

# Only download if prebuilt files aren't already present
if (-Not (Test-Path "Build/$folder") -or -Not (Test-Path "Build/src")) {
    Write-Host "Prebuilt dependencies not found - downloading $zipPath..."

    Invoke-WebRequest -Uri "https://github.com/iPlug2/iPlug2/releases/download/v1.0.0-beta/$zipFile.zip" -OutFile $zipPath -UseBasicParsing

    # Ensure Build directory exists
    if (-not (Test-Path -Path 'Build' -PathType Container)) {
        New-Item -ItemType Directory -Path 'Build' | Out-Null
    }

    Write-Host "Extracting $zipPath..."
    Expand-Archive -LiteralPath $zipPath -DestinationPath . -Force

    # Move extracted contents into Build/, only overwriting those specific folders/files
    Write-Host "Moving contents to Build directory..."

    $sourceDir = $zipFile           # e.g., IPLUG2_DEPS_WIN
    $destinationDir = "Build"

    # Iterate through each top-level item extracted from the archive
    Get-ChildItem -Path $sourceDir | ForEach-Object {
        $itemName = $_.Name
        $sourcePath = $_.FullName
        $targetPath = Join-Path $destinationDir $itemName

        # Remove the destination item if it already exists to prevent Move-Item errors
        if (Test-Path -LiteralPath $targetPath) {
            Write-Host "Removing existing item at destination: '$targetPath'"
            Remove-Item -LiteralPath $targetPath -Recurse -Force -ErrorAction Stop
        }

        Write-Host "Moving '$sourcePath' → '$targetPath'"
        Move-Item -LiteralPath $sourcePath -Destination $targetPath -Force -ErrorAction Stop
    }

    # Clean up extracted folder and zip archive
    Write-Host "Cleaning up..."
    Remove-Item -Recurse -Force -Path $sourceDir          # remove extracted folder like IPLUG2_DEPS_WIN
    Remove-Item -Force -Path '*.zip'                    # remove zip archive itself

    Write-Host "Done."
}
else {
    Write-Host "Prebuilt dependencies found in cache - skipping download."
}