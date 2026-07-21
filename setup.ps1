# Bootstrap tinyjs from a source checkout on Windows: download the txiki.js
# runtime and the WebView2 SDK header, compile the native launcher, and add
# this folder to the user PATH so `tinyjs` works from any terminal
# (-SkipPath to opt out).
# Run once after cloning:   powershell -ExecutionPolicy Bypass -File setup.ps1
#
# Needs a C++17 compiler: MinGW-w64 g++ on PATH, or the WinLibs toolchain
# installed via winget (auto-detected):
#   winget install BrechtSanders.WinLibs.POSIX.UCRT
param([switch]$SkipPath)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-Location $PSScriptRoot

$TJS_VERSION = if ($env:TJS_VERSION) { $env:TJS_VERSION } else { 'v26.6.0' }
$WEBVIEW2_VERSION = '1.0.2903.40'

# --- txiki.js runtime --------------------------------------------------------
if (-not (Test-Path 'bin\tjs.exe')) {
    Write-Host "==> downloading txiki.js $TJS_VERSION (windows-x86_64)"
    New-Item -ItemType Directory -Force bin | Out-Null
    $zip = Join-Path $env:TEMP "txiki-$PID.zip"
    Invoke-WebRequest -Uri "https://github.com/saghul/txiki.js/releases/download/$TJS_VERSION/txiki-windows-x86_64.zip" -OutFile $zip
    $dst = Join-Path $env:TEMP "txiki-$PID"
    Expand-Archive -Force $zip $dst
    Copy-Item (Join-Path $dst 'txiki-windows-x86_64\tjs.exe') 'bin\tjs.exe'
    Remove-Item -Recurse -Force $zip, $dst
}

# --- WebView2 SDK header (from the NuGet package; header-only, the vendored
# --- webview library ships its own loader so no DLL or import lib is needed) --
if (-not (Test-Path 'native\include\WebView2.h')) {
    Write-Host "==> downloading WebView2 SDK header ($WEBVIEW2_VERSION)"
    $pkg = Join-Path $env:TEMP "webview2-$PID.zip"
    Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$WEBVIEW2_VERSION" -OutFile $pkg
    $dst = Join-Path $env:TEMP "webview2-$PID"
    Expand-Archive -Force $pkg $dst
    Copy-Item (Join-Path $dst 'build\native\include\WebView2.h') 'native\include\WebView2.h'
    Remove-Item -Recurse -Force $pkg, $dst
}

# --- embed runtime/tiny.js into the launcher (native/gen-client.sh's job) ----
Write-Host '==> generating native\tiny_client.h'
$js = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'runtime\tiny.js'))
if ($js.Contains(')TINYJS')) { throw 'raw-string delimiter collision in tiny.js' }
$header = "// GENERATED from runtime/tiny.js by setup.ps1 - do not edit.`n" +
          "static const char TINY_CLIENT_JS[] = R`"TINYJS($js)TINYJS`";`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'native\tiny_client.h'), $header)

# --- find a compiler ---------------------------------------------------------
$gxx = $null
$cmd = Get-Command g++ -ErrorAction SilentlyContinue
if ($cmd) { $gxx = $cmd.Source }
if (-not $gxx) {
    # WinLibs installed via winget lands here (no PATH entry needed).
    $winlibs = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*" -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'mingw64\bin\g++.exe' } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($winlibs) { $gxx = $winlibs }
}
if (-not $gxx) {
    Write-Error @'
no C++ compiler found. Install MinGW-w64, e.g.:
    winget install BrechtSanders.WinLibs.POSIX.UCRT
then re-run setup.ps1.
'@
}

# --- compile the launcher ----------------------------------------------------
Write-Host "==> compiling launcher ($gxx)"
& $gxx -std=c++17 -O2 -static -Inative/include -Inative `
    -o native/launcher-win.exe native/launcher-win.cc -mwindows `
    -lole32 -loleaut32 -lshell32 -lshlwapi -luser32 -ladvapi32 -lversion `
    -lgdi32 -lgdiplus -lcomdlg32 -lwinmm -ldwmapi -luuid -lcrypt32
if ($LASTEXITCODE -ne 0) { throw "launcher compile failed ($LASTEXITCODE)" }

# --- put `tinyjs` on the PATH (per-user; the mac installer's symlink step) ---
# tinyjs.cmd lives in this folder, so appending the folder to the USER Path
# makes `tinyjs` resolve in any new terminal. Idempotent; -SkipPath opts out.
$onPath = $false
if (-not $SkipPath) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $userPath) { $userPath = '' }
    $parts = $userPath -split ';' | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') }
    if ($parts -contains $PSScriptRoot.TrimEnd('\')) {
        $onPath = $true
    } else {
        # SetEnvironmentVariable(User) also broadcasts WM_SETTINGCHANGE, so
        # terminals opened after this (from Explorer/Start) see it; already
        # open ones don't.
        [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $PSScriptRoot).TrimStart(';'), 'User')
        $onPath = $true
        Write-Host "==> added $PSScriptRoot to your user PATH"
        Write-Host '    (open a NEW terminal for it to take effect)'
    }
}

Write-Host '==> done'
& .\bin\tjs.exe --version
if ($onPath) {
    Write-Host 'try (in a new terminal):  tinyjs new hello; cd hello; tinyjs dev'
} else {
    Write-Host 'try:  .\tinyjs.cmd new hello; cd hello; ..\tinyjs.cmd dev'
    Write-Host '(re-run without -SkipPath to put `tinyjs` on your PATH)'
}
