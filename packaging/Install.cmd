@echo off
rem Installs PDF Bookmark for the current user (no admin rights) and adds it
rem to PATH. Double-click, or run from a terminal: Install.cmd [-NoPath]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
set "RC=%ERRORLEVEL%"
if "%~1"=="" pause
exit /b %RC%
