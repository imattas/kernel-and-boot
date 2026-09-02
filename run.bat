@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

rem Build the FAT32 image through WSL, then run it with Windows QEMU.
wsl.exe bash -lc "cd /mnt/g/os && make image"
if errorlevel 1 exit /b %errorlevel%

set "QEMU=%QEMU_SYSTEM_X86_64%"
if not defined QEMU set "QEMU=qemu-system-x86_64.exe"
set "OVMF_CODE=%OS_OVMF_CODE%"
if not defined OVMF_CODE set "OVMF_CODE=%ProgramFiles%\qemu\share\edk2-x86_64-code.fd"
set "OVMF_VARS=%OS_OVMF_VARS%"
if not defined OVMF_VARS set "OVMF_VARS=%ProgramFiles%\qemu\share\edk2-i386-vars.fd"
if not exist "%OVMF_CODE%" (
    echo OVMF code not found: "%OVMF_CODE%"
    echo Set OS_OVMF_CODE to your OVMF code firmware path.
    exit /b 2
)
if not exist "%OVMF_VARS%" (
    echo OVMF vars not found: "%OVMF_VARS%"
    echo Set OS_OVMF_VARS to your OVMF vars template path.
    exit /b 2
)

if not exist build mkdir build
copy /y "%OVMF_VARS%" build\OVMF_VARS.windows.fd >nul
if errorlevel 1 exit /b %errorlevel%

"%QEMU%" -machine pc,usb=on -smp 2 -m 128M ^
    -drive if=pflash,format=raw,readonly=on,file="%OVMF_CODE%" ^
    -drive if=pflash,format=raw,file="%SCRIPT_DIR%build\OVMF_VARS.windows.fd" ^
    -drive if=ide,index=0,media=disk,format=raw,file="%SCRIPT_DIR%dist\os.img" ^
    -device piix3-usb-uhci ^
    -device usb-kbd ^
    -display sdl -k en-us -serial file:build\qemu-run.log -no-reboot -no-shutdown ^
    -netdev user,id=osnet -device e1000,netdev=osnet
exit /b %errorlevel%
