# tinyjs installer (Windows).
#
#   irm https://tinyjs.app/install.ps1 | iex
#
# Downloads the latest release (or $env:TINYJS_VERSION = 'vX.Y.Z') into
# %LOCALAPPDATA%\tinyjs (or $env:TINYJS_HOME) and adds it to your user PATH.
# Needs the WebView2 runtime (preinstalled on Windows 11) — no git, no
# compiler. `tinyjs update` re-runs this installer.
#
# For local testing: $env:TINYJS_INSTALL_ZIP = path to a release-layout zip
# skips the download + checksum and installs that file instead.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Repo = 'tarwin/tinyjsapp'
$InstallDir = if ($env:TINYJS_HOME) { $env:TINYJS_HOME } else { Join-Path $env:LOCALAPPDATA 'tinyjs' }
$Version = if ($env:TINYJS_VERSION) { $env:TINYJS_VERSION } else { 'latest' }
$Asset = 'tinyjs-windows-x86_64.zip'

$tmp = Join-Path $env:TEMP "tinyjs-install-$PID"
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    if ($env:TINYJS_INSTALL_ZIP) {
        $zip = $env:TINYJS_INSTALL_ZIP
        Write-Host "==> installing from local zip: $zip"
    } else {
        if ($Version -eq 'latest') {
            $rel = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ 'User-Agent' = 'tinyjs-installer' }
            $Version = $rel.tag_name
            if (-not $Version) { throw 'could not determine the latest release' }
        }
        $base = "https://github.com/$Repo/releases/download/$Version"

        Write-Host "==> downloading tinyjs $Version (windows-x86_64)"
        $zip = Join-Path $tmp $Asset
        try {
            Invoke-WebRequest "$base/$Asset" -OutFile $zip
        } catch {
            throw "no Windows build in release $Version (it may predate Windows support) - $_"
        }

        Write-Host '==> verifying checksum'
        $sums = (Invoke-WebRequest "$base/checksums.txt").Content
        $line = $sums -split "`n" | Where-Object { $_ -match [regex]::Escape($Asset) } | Select-Object -First 1
        if (-not $line) { throw "checksums.txt has no entry for $Asset" }
        $expected = ($line -split '\s+')[0].Trim()
        $actual = (Get-FileHash $zip -Algorithm SHA256).Hash
        if ($actual -ne $expected -and $actual -ne $expected.ToUpper()) {
            throw "checksum verification FAILED (expected $expected, got $actual)"
        }
    }

    # Extract fresh, then swap directories. The swap (not delete-in-place)
    # matters because `tinyjs update` runs this installer FROM the install
    # dir: Windows locks a running tjs.exe against deletion, but moving its
    # directory is fine.
    Write-Host "==> installing to $InstallDir"
    $fresh = "$InstallDir.new"
    $old = "$InstallDir.old"
    Remove-Item -Recurse -Force $fresh, $old -ErrorAction SilentlyContinue
    Expand-Archive $zip $fresh
    if (Test-Path $InstallDir) { Move-Item $InstallDir $old }
    Move-Item $fresh $InstallDir
    Remove-Item -Recurse -Force $old -ErrorAction SilentlyContinue  # best effort while running

    # Add to the user PATH (idempotent; same logic as setup.ps1).
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $userPath) { $userPath = '' }
    $parts = $userPath -split ';' | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') }
    if ($parts -notcontains $InstallDir.TrimEnd('\')) {
        [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $InstallDir).TrimStart(';'), 'User')
        Write-Host "==> added $InstallDir to your user PATH (open a NEW terminal)"
    }

    $v = & (Join-Path $InstallDir 'tinyjs.cmd') version
    Write-Host "==> installed: $v"
    Write-Host 'get started (in a new terminal):  tinyjs new myapp; cd myapp; tinyjs dev'
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
