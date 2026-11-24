/* KerneliaOS AES sifreleme (FIPS 197). Bkz. aes.h */
#include "aes.h"

static uint8_t SBOX[256];
static int SBOX_READY = 0;

/* GF(2^8) carpimi (AES indirgeme polinomu x^8+x^4+x^3+x+1) */
static uint8_t gmul(uint8_t a, uint8_t b){
    uint8_t p = 0;
    for(int i = 0; i < 8; i++){
        if(b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if(hi) a ^= 0x1B;
        b >>= 1;
    }
    return p;
}

static uint8_t rotl8(uint8_t x, int s){ return (uint8_t)((x << s) | (x >> (8 - s))); }

static void sbox_init(void){
    if(SBOX_READY) return;
    for(int i = 0; i < 256; i++){
        uint8_t inv = 0;
        if(i != 0){
            /* carpimsal ters: a^254 */
            uint8_t r = 1, b = (uint8_t)i;
            for(int e = 254; e; e >>= 1){
                if(e & 1) r = gmul(r, b);
                b = gmul(b, b);
            }
            inv = r;
        }
        SBOX[i] = (uint8_t)(inv ^ rotl8(inv, 1) ^ rotl8(inv, 2) ^ rotl8(inv, 3) ^ rotl8(inv, 4) ^ 0x63);
    }
    SBOX_READY = 1;
}

int aes_init(aes_ctx *c, const uint8_t *key, size_t klen){
    sbox_init();
    int nk;
    if(klen == 16){ nk = 4; c->rounds = 10; }
    else if(klen == 32){ nk = 8; c->rounds = 14; }
    else return -1;
    int total = 4 * (c->rounds + 1);          /* sozcuk sayisi */
    for(int i = 0; i < (int)klen; i++) c->rk[i] = key[i];
    uint8_t rcon = 1;
    for(int i = nk; i < total; i++){
        uint8_t t[4];
        for(int j = 0; j < 4; j++) t[j] = c->rk[(i - 1) * 4 + j];
        if(i % nk == 0){
            uint8_t first = t[0];
            t[0] = (uint8_t)(SBOX[t[1]] ^ rcon);
            t[1] = SBOX[t[2]];
            t[2] = SBOX[t[3]];
            t[3] = SBOX[first];
            rcon = gmul(rcon, 2);
        } else if(nk > 6 && i % nk == 4){
            for(int j = 0; j < 4; j++) t[j] = SBOX[t[j]];
        }
        for(int j = 0; j < 4; j++) c->rk[i * 4 + j] = c->rk[(i - nk) * 4 + j] ^ t[j];
    }
    return 0;
}

static void add_round_key(uint8_t s[16], const uint8_t *k){
    for(int i = 0; i < 16; i++) s[i] ^= k[i];
}

void aes_encrypt_block(const aes_ctx *c, const uint8_t in[16], uint8_t out[16]){
    uint8_t s[16];
    for(int i = 0; i < 16; i++) s[i] = in[i];
    add_round_key(s, c->rk);
    for(int r = 1; r <= c->rounds; r++){
        /* SubBytes */
        for(int i = 0; i < 16; i++) s[i] = SBOX[s[i]];
        /* ShiftRows (sutun sirali durum: s[col*4 + row]) */
        uint8_t t[16];
        for(int col = 0; col < 4; col++)
            for(int row = 0; row < 4; row++)
                t[col * 4 + row] = s[((col + row) % 4) * 4 + row];
        for(int i = 0; i < 16; i++) s[i] = t[i];
        /* MixColumns (son turda yok) */
        if(r != c->rounds){
            for(int col = 0; col < 4; col++){
                uint8_t a0 = s[col*4], a1 = s[col*4+1], a2 = s[col*4+2], a3 = s[col*4+3];
                s[col*4]   = (uint8_t)(gmul(a0,2) ^ gmul(a1,3) ^ a2 ^ a3);
                s[col*4+1] = (uint8_t)(a0 ^ gmul(a1,2) ^ gmul(a2,3) ^ a3);
                s[col*4+2] = (uint8_t)(a0 ^ a1 ^ gmul(a2,2) ^ gmul(a3,3));
                s[col*4+3] = (uint8_t)(gmul(a0,3) ^ a1 ^ a2 ^ gmul(a3,2));
            }
        }
        add_round_key(s, c->rk + r * 16);
    }
    for(int i = 0; i < 16; i++) out[i] = s[i];
}
