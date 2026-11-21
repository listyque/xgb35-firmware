@echo off
"%~dp0python\python.exe" -B "%~dp0tools\recover.py" %*
set "recovery_exit=%errorlevel%"
if "%~1"=="" pause
exit /b %recovery_exit%
