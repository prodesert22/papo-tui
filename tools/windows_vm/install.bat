@echo off
rem First logon of the Windows CI VM (tools/ci.py vm create). It installs the Python that starts
rem tools/ci.py and the MSYS2 shell that runs queued jobs, then runs the job loop at every logon.
rem The CI toolchain itself is the pinned archive that tools/ci.py unpacks into C:\papo-ci.
set SHARE=\\host.lan\Data
:wait
if not exist %SHARE%\logs ( ping -n 6 127.0.0.1 >nul & goto wait )
echo install started %date% %time% >> %SHARE%\logs\status.txt
rem Builds and tests are faster without real-time scanning of their trees.
powershell -NoProfile -Command "Add-MpPreference -ExclusionPath C:\msys64,C:\papo-ci,C:\papo-ci-work,C:\Python313,C:\jobs,$env:TEMP" >> %SHARE%\logs\install.txt 2>&1
powercfg /change standby-timeout-ac 0
powercfg /change monitor-timeout-ac 0
rem A fresh system downloads root certificates only for Windows network APIs; import the
rem published set so Python can verify downloads.
certutil -generateSSTFromWU %TEMP%\roots.sst >> %SHARE%\logs\install.txt 2>&1
powershell -NoProfile -Command "Get-ChildItem $env:TEMP\roots.sst | Import-Certificate -CertStoreLocation Cert:\LocalMachine\Root | Measure-Object" >> %SHARE%\logs\install.txt 2>&1
C:\OEM\python-3.13.17-amd64.exe /quiet InstallAllUsers=1 PrependPath=1 Include_launcher=1 Include_test=0 TargetDir=C:\Python313 >> %SHARE%\logs\install.txt 2>&1
if not exist C:\msys64\usr\bin\bash.exe C:\OEM\msys2-base.sfx.exe -y -oC:\ >> %SHARE%\logs\install.txt 2>&1
C:\msys64\usr\bin\bash.exe -lc "pacman --noconfirm -Syuu" >> %SHARE%\logs\install.txt 2>&1
C:\msys64\usr\bin\bash.exe -lc "pacman --noconfirm -Syuu" >> %SHARE%\logs\install.txt 2>&1
rem Windows Update must not restart the VM in the middle of a job.
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" /v NoAutoUpdate /t REG_DWORD /d 1 /f >> %SHARE%\logs\install.txt 2>&1
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" /v NoAutoRebootWithLoggedOnUsers /t REG_DWORD /d 1 /f >> %SHARE%\logs\install.txt 2>&1
sc config wuauserv start= disabled >> %SHARE%\logs\install.txt 2>&1
net stop wuauserv >> %SHARE%\logs\install.txt 2>&1
if not exist C:\jobs mkdir C:\jobs
copy /y C:\OEM\loop.bat C:\jobs\loop.bat >nul
schtasks /create /tn PapoJobs /tr "C:\jobs\loop.bat" /sc onlogon /rl highest /f >> %SHARE%\logs\install.txt 2>&1
echo install finished %date% %time% >> %SHARE%\logs\status.txt
start "Jobs" C:\jobs\loop.bat
