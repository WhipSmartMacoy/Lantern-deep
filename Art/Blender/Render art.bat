@echo off
setlocal
title Lantern Deep - Render art
cd /d "%~dp0"
set "BL="
for /d %%D in ("%ProgramFiles%\Blender Foundation\Blender*") do if exist "%%D\blender.exe" set "BL=%%D\blender.exe"
if not defined BL for /f "delims=" %%P in ('where blender 2^>nul') do set "BL=%%P"
if not defined BL (
  echo Blender was not found. Install it from blender.org with the default options, then run this again.
  echo.
  pause
  exit /b 1
)
echo Using: %BL%
echo Rendering... this can take a few minutes. Leave this window open.
"%BL%" --background --factory-startup --python "tools\lantern_render.py" -- "jobs.json" > "render_log.txt" 2>&1
echo.
if exist "renders\_result.txt" (type "renders\_result.txt") else (echo Something went wrong. Tell Claude - the details are in render_log.txt.)
echo.
echo Finished. You can close this window and tell Claude it is done.
pause
