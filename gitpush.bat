@echo off
rem Commit everything in the folder this file is in and push it to GitHub
rem (works for any of the WSPR projects, wherever the folder is - C:, D: ...)
cd /d "%~dp0"
echo Folder: %CD%
set "msg="
set /p msg=Commit message (Enter for "Update"): 
if "%msg%"=="" set "msg=Update"
git add .
git commit -m "%msg%"
echo.
echo Pushing...
git push
echo.
git status -sb
pause
