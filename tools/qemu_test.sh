#!/bin/bash
# KerneliaOS QEMU test calistiricisi.
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

# run <ad> <beklenen_kod> <beklenen_seri_metni> [ek qemu bayraklari]
run() {
    local name="$1" want_rc="$2" want_text="$3" extra="${4:-}"
    local log="$WORK/$name.serial.log"
    rm -f "$log"
    timeout "$TIMEOUT" "$QEMU" -cdrom "$WORK/$name.iso" -m 256 -vga std \
        -display none -no-reboot \
        -serial "file:$log" \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -nic user,model=rtl8139 $extra >"$WORK/$name.qemu.log" 2>&1
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

# https: kernel TLS 1.3 istemcisi -> ana makinedeki openssl s_server (QEMU user-net: 10.0.2.2:4443)
https_scenario() {
    local c="$WORK/tlsca" cfg="$WORK/https_cfg"
    mkdir -p "$c" "$cfg" || return 1
    (
        cd "$c" &&
        openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem \
            -subj "/CN=KerneliaTest CA" -days 3650 -sha256 -addext "basicConstraints=critical,CA:TRUE" 2>/dev/null &&
        openssl req -newkey rsa:2048 -nodes -keyout srv.key -out srv.csr -subj "/CN=localhost" 2>/dev/null &&
        printf 'subjectAltName=DNS:localhost,IP:10.0.2.2\n' > san.cnf &&
        openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
            -out srv.pem -days 365 -sha256 -extfile san.cnf 2>/dev/null &&
        openssl x509 -in ca.pem -outform DER -out ca.der
    ) || return 1
    {
        echo "/* test CA: tools/qemu_test.sh tarafindan uretildi, elle degistirilmez */"
        echo "#include <stdint.h>"
        echo "static const uint8_t HTTPS_TESTCA_DER[] = {"
        od -An -v -tx1 "$c/ca.der" | sed 's/[0-9a-f][0-9a-f]/0x&,/g'
        echo "};"
        echo "static const unsigned HTTPS_TESTCA_LEN = sizeof HTTPS_TESTCA_DER;"
    } > "$cfg/tls_testca.h"
    build https "-DWIND_TEST -DWIND_TLS_TEST -DWIND_TLS_TESTCA -I$cfg" || return 1
    openssl s_server -quiet -accept 4443 -cert "$c/srv.pem" -key "$c/srv.key" \
        -tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256 -groups X25519 -www >/dev/null 2>&1 &
    local spid=$!
    sleep 1
    run https 1 "HTTPS OK" "-cpu max"
    local rc=$?
    kill "$spid" 2>/dev/null; wait "$spid" 2>/dev/null
    return $rc
}
if want https; then
    if command -v openssl >/dev/null 2>&1; then
        https_scenario || FAILS=$((FAILS+1))
    else
        echo "ATLANDI https (openssl yok)"
    fi
fi

if [ "$FAILS" -eq 0 ]; then
    echo "TUM SENARYOLAR BASARILI"
    exit 0
fi
echo "$FAILS senaryo basarisiz (kayitlar: $WORK)"
exit 1
