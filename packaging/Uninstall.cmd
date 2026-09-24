@echo off
rem Removes PDF Bookmark (this folder) and its PATH entry.
rem From a terminal, "Uninstall.cmd -Yes" skips the question.
rem This script's own folder is deleted while it runs, so: leave the folder,
rem then "(goto)" ends the batch context and the rest of this already-expanded
rem line runs without cmd returning to the deleted file.
cd /d "%TEMP%" & (goto) 2>nul & powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %* & if "%~1"=="" pause
