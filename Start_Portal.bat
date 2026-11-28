@echo off
cd /d "%~dp0"
setlocal
set "PY_CMD=python"
where python >nul 2>&1
if %errorlevel% neq 0 (
    if exist "C:\msys64\ucrt64\bin\python.exe" set "PY_CMD=C:\msys64\ucrt64\bin\python.exe"
)
echo Starting Gram Panchayat Portal with live folder record ledger...
powershell -NoProfile -Command "try { $r = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:8765/api/state' -TimeoutSec 1; exit 0 } catch { exit 1 }"
if %errorlevel% neq 0 (
    start "Rural Grievance Folder Sync" /min "%PY_CMD%" "%~dp0portal_server.py"
    timeout /t 1 /nobreak >nul
)
start "" "http://127.0.0.1:8765/index.html"
endlocal

