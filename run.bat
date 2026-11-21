@echo off
rem WindOS'u QEMU'da acar (Windows). ISO bu klasorde olmali.
rem QEMU farkli bir yere kuruluysa: set QEMU=C:\yol\qemu-system-i386.exe
if "%QEMU%"=="" set QEMU=qemu-system-i386.exe
if not exist windos.iso (
    echo windos.iso bulunamadi. Once ISO'yu bu klasore koy ^(veya CI artifact'indan indir^).
    exit /b 1
)
rem -nic user: QEMU NAT agi (misafir 10.0.2.15, ag gecidi 10.0.2.2, DNS 10.0.2.3)
"%QEMU%" -cdrom windos.iso -m 256 -vga std -nic user,model=rtl8139
