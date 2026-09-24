@echo off
rem PDF Bookmark: drag one or more PDF files onto this file.
rem For each PDF it creates "<name> (bookmarked).pdf" in the same folder.
rem The original PDF is never changed.
setlocal EnableExtensions DisableDelayedExpansion
set "APP=%~dp0pdfbookmark.exe"
set /a DONE=0
set /a NOTDONE=0
if "%~1"=="" goto :usage
if not exist "%APP%" goto :missing

:next
if "%~1"=="" goto :summary
call :one "%~1"
shift
goto :next

:usage
echo.
echo   To add bookmarks, drag one or more PDF files onto "%~nx0".
echo   See README.txt in this folder for details.
echo.
pause
exit /b 2

:missing
echo.
echo   pdfbookmark.exe was not found next to this script.
echo   Keep all files of the PDF Bookmark folder together.
echo.
pause
exit /b 1

:summary
echo.
echo   Finished: %DONE% bookmarked, %NOTDONE% not bookmarked.
echo   Details for each file are in the "pdfbookmark-reports" folder
echo   next to your PDFs.
echo.
pause
exit /b 0

:one
set "IN=%~f1"
set "NAME=%~n1"
set "OUT=%~dpn1 (bookmarked).pdf"
set "REPORTS=%~dp1pdfbookmark-reports"
echo.
echo ==== %~nx1
if /I not "%~x1"==".pdf" goto :not_pdf
if not exist "%IN%" goto :not_found
if exist "%OUT%" goto :exists
if not exist "%REPORTS%" mkdir "%REPORTS%"
"%APP%" add "%IN%" --output "%OUT%" --report "%REPORTS%\%NAME%.report.json" --plan "%REPORTS%\%NAME%.plan.json" %PDFBOOKMARK_OPTIONS%
set "RC=%ERRORLEVEL%"
if "%RC%"=="3" goto :no_plan
if "%RC%"=="4" goto :cancelled
if not "%RC%"=="0" goto :failed
echo   OK: created "%NAME% (bookmarked).pdf"
set /a DONE+=1
exit /b 0

:not_pdf
echo   Skipped: this is not a .pdf file.
set /a NOTDONE+=1
exit /b 0

:not_found
echo   Skipped: file not found.
set /a NOTDONE+=1
exit /b 0

:exists
echo   Skipped: "%NAME% (bookmarked).pdf" already exists.
echo   Delete or rename it, then try again.
set /a NOTDONE+=1
exit /b 0

:no_plan
echo   Not bookmarked: a complete, verified set of bookmarks could not be made.
echo   The lines above explain why. You can try
echo   "Add bookmarks (allow partial).cmd", which skips entries it cannot place.
set /a NOTDONE+=1
exit /b 0

:cancelled
echo   Cancelled.
set /a NOTDONE+=1
exit /b 0

:failed
echo   Not bookmarked: an error occurred - see the message above.
set /a NOTDONE+=1
exit /b 0
