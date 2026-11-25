/* X.509 ayrıştırma, zincir dogrulama (RSA ve ECDSA) ve hostname testleri.
   Sertifikalar OpenSSL ile uretildi (x509_vectors.h).
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_x509 app/test/test_x509.c && ./test_x509 */
#include <stdio.h>
#include <string.h>
#include "../crypto/bignum.c"
#include "../crypto/sha256.c"
#include "../crypto/rsa.c"
#include "../crypto/ecdsa.c"
#include "../crypto/x509.c"
#include "x509_vectors.h"

static int fails = 0, oks = 0;

static void check(const char *name, int ok){
    if(ok){ oks++; }
    else { fails++; printf("BASARISIZ: %s\n", name); }
}

int main(void){
    x509_cert ca, ca2, leaf, ecca, ecleaf;
    check("CA ayristirilir", x509_parse(&ca, CA_DER, sizeof CA_DER) == 0);
    check("CA2 ayristirilir", x509_parse(&ca2, CA2_DER, sizeof CA2_DER) == 0);
    check("leaf ayristirilir", x509_parse(&leaf, LEAF_DER, sizeof LEAF_DER) == 0);
    check("leaf sha256WithRSA", leaf.sha256_rsa == 1);
    check("ECCA ayristirilir", x509_parse(&ecca, ECCA_DER, sizeof ECCA_DER) == 0);
    check("ECLEAF ayristirilir", x509_parse(&ecleaf, ECLEAF_DER, sizeof ECLEAF_DER) == 0);
    check("ECLEAF ecdsa-SHA256", ecleaf.sha256_ecdsa == 1 && ecleaf.ec_len == 65);

    /* RSA zincir: leaf, CA tarafindan imzalanmis */
    check("leaf CA ile dogrulanir", x509_verify_issuer(&leaf, &ca) == 0);
    check("CA oz-imza dogrulanir", x509_verify_issuer(&ca, &ca) == 0);
    check("leaf CA2 ile reddedilir", x509_verify_issuer(&leaf, &ca2) == -1);

    /* ECDSA zincir: ECLEAF, ECCA tarafindan imzalanmis (ECDSA P-256) */
    check("ECCA oz-imza dogrulanir", x509_verify_issuer(&ecca, &ecca) == 0);
    check("ECLEAF ECCA ile dogrulanir", x509_verify_issuer(&ecleaf, &ecca) == 0);
    check("ECLEAF RSA CA ile reddedilir", x509_verify_issuer(&ecleaf, &ca) == -1);
    check("leaf ECCA ile reddedilir", x509_verify_issuer(&leaf, &ecca) == -1);

    /* Degistirilmis TBS (seri numarasi bolgesinde bir bayt) */
    uint8_t bad[sizeof LEAF_DER];
    memcpy(bad, LEAF_DER, sizeof LEAF_DER);
    bad[20] ^= 0x01;
    x509_cert badc;
    check("degisik leaf ayristirilir", x509_parse(&badc, bad, sizeof bad) == 0);
    check("degisik leaf reddedilir", x509_verify_issuer(&badc, &ca) == -1);

    /* Hostname: RSA leaf SAN = example.test, *.wild.test */
    check("host example.test", x509_check_hostname(&leaf, "example.test", 12) == 0);
    check("host buyuk harf", x509_check_hostname(&leaf, "EXAMPLE.test", 12) == 0);
    check("host wildcard tek etiket", x509_check_hostname(&leaf, "a.wild.test", 11) == 0);
    check("host wildcard iki etiket reddedilir", x509_check_hostname(&leaf, "a.b.wild.test", 13) == -1);
    check("host wildcard kok reddedilir", x509_check_hostname(&leaf, "wild.test", 9) == -1);
    check("host yanlis ad reddedilir", x509_check_hostname(&leaf, "evil.test", 9) == -1);
    check("bos host reddedilir", x509_check_hostname(&leaf, "", 0) == -1);
    check("ECLEAF host ec.test", x509_check_hostname(&ecleaf, "ec.test", 7) == 0);
    /* SAN'siz CA: CN'e geri donulmez */
    check("SAN yoksa eslesme yok", x509_check_hostname(&ca, "KerneliaTest CA", 15) == -1);

    /* Bozuk DER */
    static const uint8_t junk[5] = {0x30, 0x03, 0x01, 0x01, 0x00};
    x509_cert jc;
    check("bozuk DER reddedilir", x509_parse(&jc, junk, sizeof junk) == -1);

    printf("x509: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
