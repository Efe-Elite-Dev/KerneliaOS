/* RSA-PSS (SHA-256, MGF1, tuz 32) dogrulama testleri. Vektorler OpenSSL ile uretildi (pss_vectors.h).
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_pss app/test/test_pss.c && ./test_pss */
#include <stdio.h>
#include <string.h>
#include "../crypto/bignum.c"
#include "../crypto/sha256.c"
#include "../crypto/rsa.c"
#include "pss_vectors.h"

static int fails = 0, oks = 0;

static void check(const char *name, int ok){
    if(ok){ oks++; }
    else { fails++; printf("BASARISIZ: %s\n", name); }
}

int main(void){
    check("PSS imza (tuz 32) gecerli",
          rsa_pss_sha256_verify(PSS_N, sizeof PSS_N, PSS_E, sizeof PSS_E,
                                PSS_SIG, sizeof PSS_SIG, PSS_MSG, sizeof PSS_MSG) == 0);

    /* Tuz uzunlugu 20: TLS 1.3 icin reddedilmeli */
    check("tuz 20 imzasi reddedilir",
          rsa_pss_sha256_verify(PSS_N, sizeof PSS_N, PSS_E, sizeof PSS_E,
                                PSS_SIG20, sizeof PSS_SIG20, PSS_MSG, sizeof PSS_MSG) == -1);

    /* Mesajin bir biti bozuk */
    uint8_t msg[sizeof PSS_MSG];
    memcpy(msg, PSS_MSG, sizeof PSS_MSG);
    msg[5] ^= 0x02;
    check("bozuk mesaj reddedilir",
          rsa_pss_sha256_verify(PSS_N, sizeof PSS_N, PSS_E, sizeof PSS_E,
                                PSS_SIG, sizeof PSS_SIG, msg, sizeof msg) == -1);

    /* Imzanin ortasinda bir bit bozuk */
    uint8_t sig[sizeof PSS_SIG];
    memcpy(sig, PSS_SIG, sizeof PSS_SIG);
    sig[sizeof sig / 2] ^= 0x04;
    check("bozuk imza reddedilir",
          rsa_pss_sha256_verify(PSS_N, sizeof PSS_N, PSS_E, sizeof PSS_E,
                                sig, sizeof sig, PSS_MSG, sizeof PSS_MSG) == -1);

    /* s >= n */
    uint8_t big[sizeof PSS_SIG];
    memset(big, 0xFF, sizeof big);
    check("s >= n reddedilir",
          rsa_pss_sha256_verify(PSS_N, sizeof PSS_N, PSS_E, sizeof PSS_E,
                                big, sizeof big, PSS_MSG, sizeof PSS_MSG) == -1);

    /* PSS imzasi PKCS#1 v1.5 olarak kabul edilmemeli (ve tersi) */
    check("PSS imza PKCS1 ile reddedilir",
          rsa_pkcs1_sha256_verify(PSS_N, sizeof PSS_N, PSS_E, sizeof PSS_E,
                                  PSS_SIG, sizeof PSS_SIG, PSS_MSG, sizeof PSS_MSG) == -1);

    printf("pss: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
