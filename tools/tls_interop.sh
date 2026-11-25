#!/usr/bin/env bash
# KerneliaOS TLS 1.3 istemci el sikismasini OpenSSL s_server ile dogrular.
# Sunucu sertifikalari gecici olarak uretilir (CA + RSA ve ECDSA yaprak, SAN: localhost).
# Gereken: openssl (CLI), gcc.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"
cleanup(){ kill $(jobs -p) 2>/dev/null || true; rm -rf "$W"; }
trap cleanup EXIT
cd "$W"

openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem \
    -subj "/CN=KerneliaTest CA" -days 3650 -sha256 -addext "basicConstraints=critical,CA:TRUE" 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca2.key -out ca2.pem \
    -subj "/CN=Other CA" -days 3650 -sha256 2>/dev/null
printf 'subjectAltName=DNS:localhost\n' > san.cnf

openssl req -newkey rsa:2048 -nodes -keyout rsa.key -out rsa.csr -subj "/CN=localhost" 2>/dev/null
openssl x509 -req -in rsa.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -out rsa.pem -days 365 -sha256 -extfile san.cnf 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout ec.key \
    -out ec.csr -subj "/CN=localhost" 2>/dev/null
openssl x509 -req -in ec.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -out ec.pem -days 365 -sha256 -extfile san.cnf 2>/dev/null

openssl x509 -in ca.pem -outform DER -out ca.der
openssl x509 -in ca2.pem -outform DER -out ca2.der

BIN="$W/test_tls_hs"
gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o "$BIN" "$ROOT/app/test/test_tls_hs.c"

# Sunucu: TLS 1.3, tek sifre, X25519, HTTP yaniti (-www)
serve(){ # cert key port
    openssl s_server -quiet -accept "$3" -cert "$1" -key "$2" \
        -tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256 -groups X25519 -www >/dev/null 2>&1 &
}
serve rsa.pem rsa.key 4443
serve ec.pem ec.key 4444
sleep 1

fail=0
run(){ # aciklama, beklenen, port, ca, host
    if "$BIN" "$3" "$4" "$5" "$2" > "$W/out.txt" 2>&1; then
        echo "  [ok] $1: $(cat "$W/out.txt")"
    else
        echo "  [HATA] $1: $(cat "$W/out.txt")"
        fail=1
    fi
}

echo "TLS 1.3 istemci el sikisma (OpenSSL s_server):"
run "RSA-PSS sunucu, dogru CA" ok 4443 ca.der localhost
run "ECDSA P-256 sunucu, dogru CA" ok 4444 ca.der localhost
run "RSA sunucu, yanlis CA reddedilmeli" fail 4443 ca2.der localhost
run "ECDSA sunucu, yanlis CA reddedilmeli" fail 4444 ca2.der localhost
run "hostname uyusmazligi reddedilmeli" fail 4443 ca.der example.org

exit $fail
