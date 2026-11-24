/* X25519 icin host testleri: RFC 7748 bolum 6.1 (Alice/Bob) vektorleri.
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_x25519 app/test/test_x25519.c && ./test_x25519 */
#include <stdio.h>
#include <string.h>
#include "../crypto/x25519.c"

static void unhex(const char *hex, uint8_t *out, size_t n){
    for(size_t i = 0; i < n; i++){ unsigned v; sscanf(hex + 2*i, "%2x", &v); out[i] = (uint8_t)v; }
}
static int hex_eq(const uint8_t *b, size_t n, const char *hex){
    uint8_t e[64]; unhex(hex, e, n);
    return memcmp(b, e, n) == 0;
}

int main(void){
    int oks = 0, fails = 0;
    uint8_t a_priv[32], b_priv[32], a_pub[32], b_pub[32], s1[32], s2[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a_priv, 32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b_priv, 32);

    x25519_public(a_pub, a_priv);
    if(hex_eq(a_pub, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")) oks++; else { fails++; printf("BASARISIZ: Alice genel anahtar\n"); }
    x25519_public(b_pub, b_priv);
    if(hex_eq(b_pub, 32, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f")) oks++; else { fails++; printf("BASARISIZ: Bob genel anahtar\n"); }

    x25519(s1, a_priv, b_pub);
    x25519(s2, b_priv, a_pub);
    if(hex_eq(s1, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742")) oks++; else { fails++; printf("BASARISIZ: paylasilan sir (Alice)\n"); }
    if(memcmp(s1, s2, 32) == 0) oks++; else { fails++; printf("BASARISIZ: iki taraf ayni sir degil\n"); }

    printf("x25519: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
