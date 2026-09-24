@echo off
rem Runs PDF Bookmark on every PDF under tests\books (read-only) and writes
rem reports, plans, bookmarked copies and summary.txt to out\books-results.
setlocal EnableExtensions DisableDelayedExpansion
set "BOOKS=%~dp0"
set "ROOT=%~dp0..\.."
set "APP=%ROOT%\dist\pdfbookmark\pdfbookmark.exe"
set "RESULTS=%ROOT%\out\books-results"
if not exist "%APP%" goto :no_app
if not exist "%RESULTS%" mkdir "%RESULTS%"
set "SUMMARY=%RESULTS%\summary.txt"
echo PDF Bookmark results > "%SUMMARY%"
for /r "%BOOKS%" %%F in (*.pdf) do call :one "%%~fF"
echo.
echo Summary written to "%SUMMARY%"
type "%SUMMARY%"
echo.
pause
exit /b 0

:no_app
echo The packaged program was not found at "%APP%".
echo Build it first (see docs\BUILDING.md).
pause
exit /b 1

:one
set "IN=%~f1"
set "NAME=%~n1"
set "BASE=%RESULTS%\%~n1"
echo.
echo ==== %~nx1
"%APP%" analyze "%IN%" --report "%BASE%.report.json" --plan "%BASE%.plan.json" --force
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :record
if exist "%BASE% (bookmarked).pdf" del "%BASE% (bookmarked).pdf"
"%APP%" apply "%IN%" --plan "%BASE%.plan.json" --output "%BASE% (bookmarked).pdf"
set "RC=%ERRORLEVEL%"
:record
if "%RC%"=="0" echo bookmarked    %~nx1>> "%SUMMARY%"
if "%RC%"=="3" echo no-ready-plan %~nx1>> "%SUMMARY%"
if "%RC%"=="1" echo error         %~nx1>> "%SUMMARY%"
if "%RC%"=="4" echo cancelled     %~nx1>> "%SUMMARY%"
exit /b 0
