@echo off
rem The Punisher: Extended Cut - installer (put this folder itself - not its contents - inside your game folder, next to pun.exe)
cd /d "%~dp0"
set PY=
where py >nul 2>nul && set PY=py -3
if not defined PY (where python >nul 2>nul && set PY=python)
if not defined PY (
  echo Python 3 is needed to run the installer: https://www.python.org/downloads/  ^(tick "Add python.exe to PATH"^)
  pause
  exit /b 1
)
%PY% ec_install.py install "%~dp0.."
echo.
pause
