/* WindOS fiziksel bellek ayiricisi: 4 KB cerceveler, bitmap (1 = kullanimda).
   Heap (kmalloc/kfree) app/heap.c'de. Donanima bagli degildir; host testi app/test/test_mem.c. */
#ifndef WIND_MEM_C
#define WIND_MEM_C

/* ------------------------------------------------------------------------ */
/* Fiziksel cerceveler: 4 KB'lik birimler, bitmap (1 = kullanimda)          */
/* ------------------------------------------------------------------------ */
#define FRAME_SIZE   4096ull
#define FRAME_LIMIT  (512ull * 1024ull * 1024ull)      /* 512 MB'a kadar yonetilir */
#define FRAME_MAX    ((unsigned)(FRAME_LIMIT / FRAME_SIZE))

static unsigned char FRAME_BMP[FRAME_MAX / 8];
static unsigned FRAME_FREE_N = 0;      /* bos cerceve sayisi */
static unsigned FRAME_HINT = 0;        /* arama baslangici (dairesel) */

static int frame_used(unsigned f){ return (FRAME_BMP[f >> 3] >> (f & 7u)) & 1u; }

/* Tum cerceveleri kullanimda isaretler; sonra mem_init bos bolgeleri acar */
static void frame_init(void){
    for(unsigned i = 0; i < FRAME_MAX / 8; i++) FRAME_BMP[i] = 0xFF;
    FRAME_FREE_N = 0;
    FRAME_HINT = 0;
}

/* [start, end) araligini isaretler. used=1: kullanimda (disariya dogru yuvarlanir,
   yani kismen kapsanan cerceve de ayrilir). used=0: bos (yalnizca tam kapsananlar acilir). */
static void frame_range(unsigned long long start, unsigned long long end, int used){
    if(end > FRAME_LIMIT) end = FRAME_LIMIT;
    if(start >= end) return;
    unsigned long long f0, f1;
    if(used){
        f0 = start / FRAME_SIZE;
        f1 = (end + FRAME_SIZE - 1) / FRAME_SIZE;
    } else {
        f0 = (start + FRAME_SIZE - 1) / FRAME_SIZE;
        f1 = end / FRAME_SIZE;
    }
    if(f1 > FRAME_MAX) f1 = FRAME_MAX;
    for(unsigned f = (unsigned)f0; f < (unsigned)f1; f++){
        int was = frame_used(f);
        if(used && !was){ FRAME_BMP[f >> 3] |= (unsigned char)(1u << (f & 7u)); FRAME_FREE_N--; }
        else if(!used && was){ FRAME_BMP[f >> 3] &= (unsigned char)~(1u << (f & 7u)); FRAME_FREE_N++; }
    }
}

/* Bos bir cerceve ayirir; fiziksel adres doner. Bos cerceve yoksa 0 (cerceve 0 hep kullanimda). */
static unsigned long long frame_alloc(void){
    for(unsigned k = 0; k < FRAME_MAX; k++){
        unsigned f = (FRAME_HINT + k) % FRAME_MAX;
        if(!frame_used(f)){
            FRAME_BMP[f >> 3] |= (unsigned char)(1u << (f & 7u));
            FRAME_FREE_N--;
            FRAME_HINT = f + 1;
            return (unsigned long long)f * FRAME_SIZE;
        }
    }
    return 0;
}

/* Fiziksel adresi serbest birakir. Kullanimda olmayan ya da aralik disi adres sessizce yok sayilir. */
static void frame_free(unsigned long long pa){
    if(pa >= FRAME_LIMIT) return;
    unsigned f = (unsigned)(pa / FRAME_SIZE);
    if(!frame_used(f)) return;
    FRAME_BMP[f >> 3] &= (unsigned char)~(1u << (f & 7u));
    FRAME_FREE_N++;
    if(f < FRAME_HINT) FRAME_HINT = f;
}

#endif
