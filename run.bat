@echo off
title QEMU ISO Launcher

:: QEMU kurulum dizini (kendi sistemine gore kontrol et)
set "QEMU_DIR=C:\Program Files\qemu"
set "QEMU_EXE=%QEMU_DIR%\qemu-system-x86_64.exe"

:: Suruklenen dosya var mi kontrol et
if "%~1"=="" (
    echo [HATA] Lutfen calistirmak istedigin .iso dosyasini bu .bat dosyasinin uzerine surukle!
    pause
    exit /b 1
)

:: QEMU calistirilabilir dosyasi var mi kontrol et
if not exist "%QEMU_EXE%" (
    echo [HATA] qemu-system-x86_64.exe bulunamadi: "%QEMU_EXE%"
    echo Lutfen script icindeki QEMU_DIR yolunu duzenle.
    pause
    exit /b 1
)

echo Calistirilan ISO: "%~1"
echo QEMU baslatiliyor...

:: -nic user: QEMU NAT agi (misafir 10.0.2.15, ag gecidi 10.0.2.2, DNS 10.0.2.3)
"%QEMU_EXE%" ^
  -m 2048 ^
  -smp 2 ^
  -cdrom "%~1" ^
  -boot order=d ^
  -vga std ^
  -display sdl ^
  -nic user,model=rtl8139

if %errorlevel% neq 0 (
    echo.
    echo [HATA] QEMU beklenmedik bir kod ile kapandi: %errorlevel%
    pause
)
