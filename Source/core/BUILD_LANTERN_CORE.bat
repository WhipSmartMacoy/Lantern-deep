@echo off
rem Builds the Lantern Deep core with Quartus and puts a ready-to-copy SD card folder in .\SD
rem Takes about 10-20 minutes. Leave this window open until it says DONE.
setlocal
set QBIN=C:\intelFPGA_lite\21.1\quartus\bin64
set HERE=%~dp0
if not exist "%QBIN%\quartus_sh.exe" (
  echo Quartus was not found in %QBIN%
  pause
  exit /b 1
)
cd /d "%HERE%openfpga-litex\projects"
echo Building the core... (log: %HERE%build_log.txt)
"%QBIN%\quartus_sh.exe" --flow compile openFPGA-RISC-V_pocket.qpf > "%HERE%build_log.txt" 2>&1
if errorlevel 1 (
  echo.
  echo BUILD FAILED. Send Claude the file build_log.txt from this folder.
  pause
  exit /b 1
)
if exist "%HERE%SD" rmdir /s /q "%HERE%SD"
xcopy /e /i /q "%HERE%SD_template" "%HERE%SD" > nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%tools\reverse_rbf.ps1" -In "output_files\openFPGA-RISC-V_pocket.rbf" -Out "%HERE%SD\Cores\parke.LanternDeep\lanterndeep.rev"
if errorlevel 1 (
  echo Could not convert the bitstream.
  pause
  exit /b 1
)
rem reports Claude wants to see (resource use and timing)
copy /y "output_files\openFPGA-RISC-V_pocket.fit.summary" "%HERE%fit_summary.txt" > nul
copy /y "output_files\openFPGA-RISC-V_pocket.sta.summary" "%HERE%timing_summary.txt" > nul
echo.
echo DONE. Copy the Cores, Platforms and Assets folders from .\SD to the root of the Pocket's SD card.
pause
