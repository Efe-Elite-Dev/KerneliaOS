#!/bin/bash
# WindOS'u QEMU'da acar. ISO yoksa once derler.
# Kullanim: ./run.sh [iso-dosyasi]
set -e
ISO="${1:-windos.iso}"
if [ ! -f "$ISO" ]; then
    echo "[run] $ISO yok, derleniyor..."
    make iso
fi
QEMU="${QEMU:-qemu-system-i386}"
# -nic user: QEMU NAT agi (misafir 10.0.2.15, ag gecidi 10.0.2.2, DNS 10.0.2.3)
exec "$QEMU" -cdrom "$ISO" -m 256 -vga std -nic user,model=rtl8139
