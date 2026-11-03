# Bootstrap tinyjs from a source checkout on Windows: download the txiki.js
# runtime and the WebView2 SDK header, compile the native launcher, and add
# this folder to the user PATH so `tinyjs` works from any terminal
# (-SkipPath to opt out).
# Run once after cloning:   powershell -ExecutionPolicy Bypass -File setup.ps1
#
# Needs a C++17 compiler: MinGW-w64 g++ on PATH, or the WinLibs toolchain
# installed via winget (auto-detected):
#   winget install BrechtSanders.WinLibs.POSIX.UCRT
# On Windows on ARM, llvm-mingw builds a native launcher (auto-detected; g++
# there makes an x64 one that runs emulated):
#   winget install MartinStorsjo.LLVM-MinGW.UCRT
# $env:CXX overrides the compiler search.
param([switch]$SkipPath)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-Location $PSScriptRoot

$TJS_VERSION = if ($env:TJS_VERSION) { $env:TJS_VERSION } else { 'v26.11.0' }
$WEBVIEW2_VERSION = '1.0.2903.40'
$REPO = 'tarwin/tinyjsapp'

# The machine's architecture, read from the registry rather than this
# process's own $env:PROCESSOR_ARCHITECTURE: an x64 PowerShell running under
# emulation on ARM64 reports AMD64.
$machineArch = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -ErrorAction SilentlyContinue).PROCESSOR_ARCHITECTURE
$ARCH = if ($machineArch -eq 'ARM64' -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x86_64' }

# --- txiki.js runtime, ARM64 ---------------------------------------------------
# txiki publishes no Windows ARM64 build; our release CI builds one from
# source (the twin of tjs-linux-<arch>.gz). No prebuilt yet (an older
# release) → fall through to the x64 download below, which runs emulated.
if ($ARCH -eq 'arm64' -and -not (Test-Path 'bin\tjs.exe')) {
    $rel = if ($env:TINYJS_TJS_RELEASE) { "download/$env:TINYJS_TJS_RELEASE" } else { 'latest/download' }
    $zip = Join-Path $env:TEMP "tjs-arm64-$PID.zip"
    Write-Host "==> downloading tjs (windows-arm64) from $REPO releases"
    try {
        Invoke-WebRequest -Uri "https://github.com/$REPO/releases/$rel/tjs-windows-arm64.zip" -OutFile $zip -UseBasicParsing
        New-Item -ItemType Directory -Force bin | Out-Null
        Expand-Archive -Force $zip bin
    } catch {
        Write-Host '    (no prebuilt arm64 tjs in that release - using the x64 one, which runs emulated)'
    } finally {
        Remove-Item -Force $zip -ErrorAction SilentlyContinue
    }
}

# --- txiki.js runtime --------------------------------------------------------
if (-not (Test-Path 'bin\tjs.exe')) {
    Write-Host "==> downloading txiki.js $TJS_VERSION (windows-x86_64)"
    New-Item -ItemType Directory -Force bin | Out-Null
    $zip = Join-Path $env:TEMP "txiki-$PID.zip"
    Invoke-WebRequest -Uri "https://github.com/saghul/txiki.js/releases/download/$TJS_VERSION/txiki-windows-x86_64.zip" -OutFile $zip
    $dst = Join-Path $env:TEMP "txiki-$PID"
    Expand-Archive -Force $zip $dst
    # Checked against the pinned hash (runtime/txiki.sha256, #26).
    $exe = Join-Path $dst 'txiki-windows-x86_64\tjs.exe'
    $key = "$TJS_VERSION/txiki-windows-x86_64"
    $want = (Get-Content 'runtime\txiki.sha256' | ForEach-Object { $f = -split $_; if ($f[1] -eq $key) { $f[0] } }) | Select-Object -First 1
    $got = (Get-FileHash -Algorithm SHA256 $exe).Hash.ToLower()
    if (-not $want) {
        Remove-Item -Recurse -Force $zip, $dst
        throw "no pinned sha256 for $key in runtime\txiki.sha256 (got $got) - add it first"
    }
    if ($got -ne $want) {
        Remove-Item -Recurse -Force $zip, $dst
        throw "txiki.js $TJS_VERSION (windows-x86_64) has sha256 $got, expected $want - refusing to install it"
    }
    Copy-Item $exe 'bin\tjs.exe'
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
if ($env:CXX) { $gxx = $env:CXX }
if (-not $gxx -and $ARCH -eq 'arm64') {
    # llvm-mingw, for a native ARM64 launcher: on PATH, or where winget puts it.
    $cmd = Get-Command aarch64-w64-mingw32-clang++ -ErrorAction SilentlyContinue
    if ($cmd) { $gxx = $cmd.Source }
    if (-not $gxx) {
        $gxx = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW*" -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ChildItem $_.FullName -Directory } |
            ForEach-Object { Join-Path $_.FullName 'bin\aarch64-w64-mingw32-clang++.exe' } |
            Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    if (-not $gxx) {
        Write-Host '    (no llvm-mingw found - the launcher will be x64 and run emulated;'
        Write-Host '     winget install MartinStorsjo.LLVM-MinGW.UCRT for a native one)'
    }
}
if (-not $gxx) {
    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($cmd) { $gxx = $cmd.Source }
}
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
# tinyjs.cmd (cmd/PowerShell) and the sh wrapper tinyjs (Git Bash) live in
# this folder, so appending the folder to the USER Path makes `tinyjs`
# resolve in any new terminal. Idempotent; -SkipPath opts out.
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
