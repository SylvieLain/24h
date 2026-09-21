@echo off
setlocal
chcp 65001 >nul
set "PYTHONUTF8=1"
set "DAP_ROOT=%~dp0..\"
if exist "%DAP_ROOT%.venv\Scripts\python.exe" (
    "%DAP_ROOT%.venv\Scripts\python.exe" "%~dp0dap.py" %*
) else (
    py -3 "%~dp0dap.py" %*
)
exit /b %errorlevel%
