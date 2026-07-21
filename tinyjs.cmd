@echo off
rem tinyjs CLI wrapper (Windows). Add this checkout to your PATH, or call it
rem by full path; setup.ps1 must have run once (downloads bin\tjs.exe and
rem compiles the launcher).
"%~dp0bin\tjs.exe" run "%~dp0cli.js" %*
