#!/bin/bash
# WindOS QEMU test calistiricisi.
# Her senaryo icin kerneli WIND_TEST bayragiyla derler, GRUB ISO'su olusturur, QEMU'da acar,
# seri ciktiyi (COM1) ve isa-debug-exit cikis kodunu kontrol eder.
#
# Senaryolar:
#   pass   : -DWIND_TEST                       -> seri "WIND_TEST: PASS", cikis kodu 1
#   fail   : -DWIND_TEST -DWIND_TEST_FAIL      -> seri "WIND_TEST: FAIL", cikis kodu 3
#   panic  : -DWIND_TEST -DWIND_TEST_PANIC     -> seri "PANIC: CEKIRDEK HATASI", cikis kodu 5
#
# Kullanim: tools/qemu_test.sh            (tum senaryolar)
#           tools/qemu_test.sh pass       (tek senaryo)
# Gereken araclar: nasm, gcc (-m32 destekli), ld, grub-mkrescue, qemu-system-i386, timeout
# Cikis kodu: tum secili senaryolar beklendigi gibi ise 0, degilse 1.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1
QEMU="${QEMU:-qemu-system-i386}"
TIMEOUT="${TIMEOUT:-60}"
WORK="${WORK:-$ROOT/build/qtest}"
rm -rf "$WORK"; mkdir -p "$WORK"

CFLAGS="-m32 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall"

# build <ad> <ek bayraklar> : $WORK/<ad>.iso uretir
build() {
    local name="$1" extra="$2" d="$WORK/$1"
    mkdir -p "$d/iso/boot/grub" || return 1
    nasm -f elf32 boot.asm -o "$d/boot.o" || return 1
    gcc $CFLAGS $extra -c kernel.c -o "$d/kernel.o" || return 1
    ld -m elf_i386 -z noexecstack -T linker.ld "$d/boot.o" "$d/kernel.o" -o "$d/kernel.bin" || return 1
    cp "$d/kernel.bin" "$d/iso/boot/kernel.bin"
    cp grub.cfg "$d/iso/boot/grub/grub.cfg"
    grub-mkrescue -o "$WORK/$name.iso" "$d/iso" >"$d/grub.log" 2>&1 || return 1
}

# run <ad> <beklenen_kod> <beklenen_seri_metni>
run() {
    local name="$1" want_rc="$2" want_text="$3"
    local log="$WORK/$name.serial.log"
    rm -f "$log"
    timeout "$TIMEOUT" "$QEMU" -cdrom "$WORK/$name.iso" -m 256 -vga std \
        -display none -no-reboot \
        -serial "file:$log" \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -nic user,model=rtl8139 >"$WORK/$name.qemu.log" 2>&1
    local rc=$?
    local ok=1
    [ "$rc" -eq "$want_rc" ] || ok=0
    grep -aq "$want_text" "$log" 2>/dev/null || ok=0
    if [ "$ok" -eq 1 ]; then
        echo "GECTI  $name  (cikis $rc, seri: \"$want_text\")"
        return 0
    fi
    echo "BASARISIZ $name  (cikis $rc, beklenen $want_rc; seri \"$want_text\" arandi)"
    echo "--- seri log (son 30 satir) ---"
    tail -n 30 "$log" 2>/dev/null
    echo "--------------------------------"
    return 1
}

SCEN="${1:-all}"
FAILS=0

want() { [ "$SCEN" = "all" ] || [ "$SCEN" = "$1" ]; }

if want pass; then
    build pass "-DWIND_TEST" && run pass 1 "WIND_TEST: PASS" || FAILS=$((FAILS+1))
fi
if want fail; then
    build fail "-DWIND_TEST -DWIND_TEST_FAIL" && run fail 3 "WIND_TEST: FAIL" || FAILS=$((FAILS+1))
fi
if want panic; then
    build panic "-DWIND_TEST -DWIND_TEST_PANIC" && run panic 5 "PANIC: CEKIRDEK HATASI" || FAILS=$((FAILS+1))
fi

if [ "$FAILS" -eq 0 ]; then
    echo "TUM SENARYOLAR BASARILI"
    exit 0
fi
echo "$FAILS senaryo basarisiz (kayitlar: $WORK)"
exit 1
