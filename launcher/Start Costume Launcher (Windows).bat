@echo off
rem Punisher Costume Launcher - Windows start (v0.8.7). Needs Python 3 with Pillow and numpy:
rem     py -m pip install pillow numpy
cd /d "%~dp0"
set PY=python
where py >nul 2>nul && set PY=py
%PY% launcher.py %*
if errorlevel 1 (
    echo.
    echo The launcher stopped with an error - see the text above and launcher.log.
    pause
)
