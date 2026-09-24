@echo off
rem Same as "Add bookmarks.cmd", but when some table-of-contents entries
rem cannot be placed reliably it skips them (and lists them in the report)
rem instead of making no bookmarks. Entries whose nesting is unclear are put
rem at the top level. Nothing is ever guessed.
setlocal
set "PDFBOOKMARK_OPTIONS=--allow-partial --flat-outline"
call "%~dp0Add bookmarks.cmd" %*
