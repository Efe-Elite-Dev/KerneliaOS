/* KerneliaOS heap ayiricisi (kmalloc / kfree). Cekirdek ve kullanici kutuphanesi (app/ulib)
   ortak kullanir; donanima bagli degildir. Tipler icin yalnizca unsigned kullanir.
   Kullanim: once heap_init(bellek, uzunluk), sonra kmalloc/kfree. */
#ifndef WIND_HEAP_C
#define WIND_HEAP_C

/* ------------------------------------------------------------------------ */
/* Heap: ilk uygun (first-fit) ayirici, bloklar bolunur ve bitisik bosluklar */
/* birlesir. Her blok 16 baytlik bir baslik tasir; veri 16 bayt hizalidir.    */
/* Bloklar ardisik ve boyutlarla zincirlenir: sonraki blok = off + size.     */
/* ------------------------------------------------------------------------ */
#define HEAP_MAGIC 0x57494E44u     /* 'WIND' */
#define HEAP_HDR   16u

typedef struct { unsigned size; unsigned used; unsigned magic; unsigned pad; } heap_hdr;

static unsigned char *HEAP_BASE = 0;
static unsigned HEAP_LEN = 0;

static heap_hdr *heap_at(unsigned off){ return (heap_hdr *)(void *)(HEAP_BASE + off); }
static unsigned heap_align(unsigned n){ return (n + 15u) & ~15u; }

/* Bloklar gecerli mi (sihirli sayi, boyut sinirlari) */
static int heap_ok(heap_hdr *h, unsigned off){
    return h->magic == HEAP_MAGIC && h->size >= HEAP_HDR && h->size <= HEAP_LEN - off && (h->size & 15u) == 0;
}

/* base: heap bellegi (len bayt). Donus: 0 basarili, -1 hatali parametre. */
static int heap_init(void *base, unsigned len){
    len &= ~15u;
    if(!base || len < 2u * HEAP_HDR) return -1;
    HEAP_BASE = (unsigned char *)base;
    HEAP_LEN = len;
    heap_hdr *h = heap_at(0);
    h->size = len;
    h->used = 0;
    h->magic = HEAP_MAGIC;
    h->pad = 0;
    return 0;
}

/* Bitisik bos bloklari birlestirir */
static void heap_merge(void){
    unsigned off = 0;
    while(off < HEAP_LEN){
        heap_hdr *h = heap_at(off);
        if(!heap_ok(h, off)) return;
        if(!h->used){
            unsigned nxt = off + h->size;
            while(nxt < HEAP_LEN){
                heap_hdr *n = heap_at(nxt);
                if(!heap_ok(n, nxt)) return;
                if(n->used) break;
                h->size += n->size;
                nxt = off + h->size;
            }
        }
        off += h->size;
    }
}

/* n bayt ayirir (16 bayt hizali). Yer yoksa ya da heap bozuksa 0 doner. */
static void *kmalloc(unsigned n){
    if(!HEAP_BASE || n == 0 || n > HEAP_LEN - HEAP_HDR) return 0;
    unsigned need = heap_align(n) + HEAP_HDR;
    unsigned off = 0;
    while(off < HEAP_LEN){
        heap_hdr *h = heap_at(off);
        if(!heap_ok(h, off)) return 0;
        if(!h->used && h->size >= need){
            if(h->size >= need + HEAP_HDR + 16u){     /* kalan parca kullanilabilir olsun */
                heap_hdr *r = heap_at(off + need);
                r->size = h->size - need;
                r->used = 0;
                r->magic = HEAP_MAGIC;
                r->pad = 0;
                h->size = need;
            }
            h->used = 1;
            return (void *)(h + 1);
        }
        off += h->size;
    }
    return 0;
}

/* kmalloc ile ayrilmis bir bloğu serbest birakir. Gecersiz adres sessizce yok sayilir. */
static void kfree(void *p){
    if(!p || !HEAP_BASE) return;
    unsigned char *c = (unsigned char *)p;
    if(c < HEAP_BASE + HEAP_HDR || c >= HEAP_BASE + HEAP_LEN) return;
    unsigned target = (unsigned)(c - HEAP_BASE) - HEAP_HDR;
    unsigned off = 0;
    while(off < HEAP_LEN){
        heap_hdr *h = heap_at(off);
        if(!heap_ok(h, off)) return;
        if(off == target){
            if(!h->used) return;                      /* cift serbest birakma */
            h->used = 0;
            heap_merge();
            return;
        }
        off += h->size;
    }
}

/* Bos veri baytlarinin toplami (baslik boyutlari dusulmus) */
static unsigned heap_free_total(void){
    unsigned total = 0, off = 0;
    while(HEAP_BASE && off < HEAP_LEN){
        heap_hdr *h = heap_at(off);
        if(!heap_ok(h, off)) break;
        if(!h->used) total += h->size - HEAP_HDR;
        off += h->size;
    }
    return total;
}

/* Kendi kendine sinama: ayir, birbirine yaz, serbest birak, birlestirmeyi dogrula.
   Donus: 0 gecti, -1 basarisiz. Heap'i bozmadan geri birakir. */
static int heap_selftest(void){
    unsigned before = heap_free_total();
    unsigned char *a = (unsigned char *)kmalloc(100);
    unsigned char *b = (unsigned char *)kmalloc(3000);
    unsigned char *c = (unsigned char *)kmalloc(64);
    if(!a || !b || !c) return -1;
    if(((unsigned)(unsigned long)a & 15u) || ((unsigned)(unsigned long)b & 15u)) return -1;
    for(unsigned i = 0; i < 100; i++) a[i] = 0xA5;
    for(unsigned i = 0; i < 3000; i++) b[i] = 0x5A;
    if(a[99] != 0xA5 || b[2999] != 0x5A) return -1;     /* ayni bolgeye yazilmamis olmali */
    kfree(b);                                           /* ortadaki blok bosalsin */
    unsigned char *d = (unsigned char *)kmalloc(2000);  /* bosluga sigmali (first-fit) */
    if(!d || d != b) return -1;
    kfree(a); kfree(c); kfree(d);
    if(heap_free_total() != before) return -1;          /* birlesmeler tam olmali */
    return 0;
}

#endif
