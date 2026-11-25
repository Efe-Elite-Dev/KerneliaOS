/* KerneliaOS RDRAND tabanli rastgelelik. Bkz. rng.h */
#include "rng.h"

int rng_available(void){
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    (void)a; (void)b; (void)d;
    return (c >> 30) & 1;
}

/* Tek 32 bitlik RDRAND; CF=0 ise (kaynak tukenmis) en fazla 10 deneme */
static int rdrand32(uint32_t *v){
    for(int i = 0; i < 10; i++){
        unsigned char ok;
        __asm__ volatile("rdrand %0\n\tsetc %1" : "=r"(*v), "=qm"(ok) : : "cc");
        if(ok) return 0;
    }
    return -1;
}

int rng_fill(uint8_t *out, size_t n){
    if(!rng_available()) return -1;
    size_t i = 0;
    while(i < n){
        uint32_t v;
        if(rdrand32(&v) != 0) return -1;
        for(int k = 0; k < 4 && i < n; k++, i++){
            out[i] = (uint8_t)(v >> (8 * k));
        }
    }
    return 0;
}
