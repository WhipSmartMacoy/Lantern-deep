@echo off
rem Lantern Deep - builds the game on this PC: new art in, new boot.bin into "Pocket SD", new PC test page.
rem Needs Python 3 (with Pillow + NumPy) and LLVM for Windows. Log: build_game_log.txt
title Lantern Deep - build
cd /d "%~dp0Source\game"
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY (
  where python >nul 2>nul && set "PY=python"
)
if not defined PY (
  echo Python was not found. Install it from python.org and tick "Add python.exe to PATH".
  echo Python was not found.> "%~dp0build_game_log.txt"
  pause
  exit /b 1
)
%PY% tools\build_windows.py %*
echo.
pause
