@echo off
rem Job loop of the Windows CI VM: runs \\host.lan\Data\jobs\job.sh when tools/ci.py queues one.
set SHARE=\\host.lan\Data
:wait
if not exist %SHARE%\logs ( ping -n 6 127.0.0.1 >nul & goto wait )
set MSYSTEM=MSYS
set CHERE_INVOKING=1
echo loop started %date% %time% >> %SHARE%\logs\status.txt
:loop
if exist %SHARE%\jobs\job.sh (
  move /y %SHARE%\jobs\job.sh C:\jobs\job.sh >nul
  echo job started %date% %time% >> %SHARE%\logs\status.txt
  C:\msys64\usr\bin\bash.exe -lc "/c/jobs/job.sh" > %SHARE%\logs\job.txt 2>&1
  call echo job exit=%%errorlevel%% >> %SHARE%\logs\status.txt
)
ping -n 6 127.0.0.1 >nul
goto loop
