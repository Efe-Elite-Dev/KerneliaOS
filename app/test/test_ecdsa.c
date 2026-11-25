/* ECDSA P-256 testleri: n*G = O, OpenSSL imzasi, bozuk veriler.
   Vektorler OpenSSL ile uretildi (ecdsa_vectors.h).
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_ecdsa app/test/test_ecdsa.c && ./test_ecdsa */
#include <stdio.h>
#include <string.h>
#include "../crypto/bignum.c"
#include "../crypto/sha256.c"
#include "../crypto/ecdsa.c"
#include "ecdsa_vectors.h"

static int fails = 0, oks = 0;

static void check(const char *name, int ok){
    if(ok){ oks++; }
    else { fails++; printf("BASARISIZ: %s\n", name); }
}

int main(void){
    /* Egri sabitleri: n*G = O (G'nin mertebesi n). Sabitler hatali ise bu tutmaz. */
    bn_ctx_init(&CP, FP, EL);
    bn_ctx_init(&CN, FN, EL);
    make_exp(EXP_P, FP);
    make_exp(EXP_N, FN);

    ec_pt G, R;
    cpy(G.x, GX); cpy(G.y, GY); G.inf = 0;
    uint8_t nb[32];
    bn_to_be(nb, 32, FN, EL);
    pt_mul(&R, nb, &G);
    check("n*G = O", R.inf == 1);

    /* (n-1)*G = -G: x ayni, y = p - Gy */
    uint32_t nm1[EL];
    cpy(nm1, FN);
    nm1[0] -= 1;
    uint8_t nmb[32];
    bn_to_be(nmb, 32, nm1, EL);
    pt_mul(&R, nmb, &G);
    uint32_t zero[EL] = {0}, negy[EL];
    bn_submod(negy, zero, GY, FP, EL);
    check("(n-1)G = -G", !R.inf && eq(R.x, GX) && eq(R.y, negy));

    /* Gercek OpenSSL imzasi */
    uint8_t h[32];
    sha256(MSG, sizeof MSG, h);
    check("OpenSSL imzasi gecerli", ecdsa_p256_verify(PUB, SIG, sizeof SIG, h) == 0);

    /* Yanlis ozet */
    uint8_t h2[32];
    memcpy(h2, h, 32);
    h2[0] ^= 1;
    check("yanlis ozet reddedilir", ecdsa_p256_verify(PUB, SIG, sizeof SIG, h2) == -1);

    /* Bozuk imza (son bayt civarinda s degeri) */
    uint8_t sig[sizeof SIG];
    memcpy(sig, SIG, sizeof SIG);
    sig[sizeof sig - 5] ^= 0x01;
    check("bozuk imza reddedilir", ecdsa_p256_verify(PUB, sig, sizeof sig, h) == -1);

    /* Egride olmayan anahtar: y koordinatinda bir bit */
    uint8_t pub[65];
    memcpy(pub, PUB, 65);
    pub[40] ^= 0x01;
    check("egride olmayan anahtar reddedilir", ecdsa_p256_verify(pub, SIG, sizeof SIG, h) == -1);

    /* r = 0 */
    static const uint8_t zr[8] = {0x30, 0x06, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01};
    check("r=0 reddedilir", ecdsa_p256_verify(PUB, zr, sizeof zr, h) == -1);

    /* Bozuk DER */
    static const uint8_t junk[4] = {0x30, 0x02, 0x02, 0x00};
    check("bozuk DER reddedilir", ecdsa_p256_verify(PUB, junk, sizeof junk, h) == -1);

    printf("ecdsa: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
