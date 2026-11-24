/* mem.c (fiziksel cerceve ayirici + kmalloc/kfree heap'i) icin host testleri.
   Derleme ve calistirma:
     gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_mem app/test/test_mem.c && ./test_mem */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../heap.c"
#include "../mem.c"

static int fails = 0, oks = 0;
#define CHECK(name, cond) do { if(cond){ oks++; printf("ok   %s\n", name); } \
                               else { fails++; printf("FAIL %s\n", name); } } while(0)

/* ---- heap ---- */
static void heap_tests(void){
    static unsigned char arena[64 * 1024] __attribute__((aligned(16)));
    CHECK("heap_init", heap_init(arena, sizeof arena) == 0);
    CHECK("heap_init bos taban reddedilir", heap_init(0, 1024) != 0);
    unsigned total0 = heap_free_total();
    CHECK("bos toplam = arena - baslik", total0 == sizeof arena - HEAP_HDR);

    void *a = kmalloc(1);
    CHECK("kmalloc(1) != 0", a != 0);
    CHECK("16 bayt hizali", ((unsigned long)a & 15ul) == 0);
    CHECK("kmalloc(0) = 0", kmalloc(0) == 0);
    CHECK("asiri boyut reddedilir", kmalloc(sizeof arena) == 0);

    /* veri birbirine karismamali */
    unsigned char *p[5];
    for(int i = 0; i < 5; i++){
        p[i] = kmalloc(100 + (unsigned)i * 37);
        CHECK("kmalloc blok", p[i] != 0);
        memset(p[i], 0x10 + i, 100 + (unsigned)i * 37);
    }
    int ok = 1;
    for(int i = 0; i < 5; i++)
        for(unsigned k = 0; k < 100u + (unsigned)i * 37u; k++) if(p[i][k] != 0x10 + i) ok = 0;
    CHECK("bloklar ust uste binmiyor", ok);

    /* serbest birakma ve birlesme: tum bloklar bosalinca tek blok kalmali */
    kfree(a);
    for(int i = 0; i < 5; i++) kfree(p[i]);
    CHECK("birlesme: bos toplam geri geldi", heap_free_total() == total0);
    void *big = kmalloc(sizeof arena / 2);
    CHECK("birlesmis blokta buyuk ayirma", big != 0);
    kfree(big);

    /* cift serbest birakma zarar vermemeli */
    void *c = kmalloc(64);
    kfree(c);
    unsigned t1 = heap_free_total();
    kfree(c);
    CHECK("cift kfree zarar vermez", heap_free_total() == t1);

    /* gecersiz adres */
    kfree((void *)(arena + 3));
    kfree(0);
    CHECK("gecersiz kfree yok sayilir", heap_free_total() == total0);

    CHECK("heap_selftest", heap_selftest() == 0);
    CHECK("heap_selftest sonrasi toplam ayni", heap_free_total() == total0);

    /* tukenme: bitene kadar ayir, sonra yer kalmamali */
    int n = 0;
    while(kmalloc(1024)) n++;
    CHECK("tukenme ve sonra 0", n > 0 && kmalloc(1024) == 0);
}

/* ---- fiziksel cerceveler ---- */
static void frame_tests(void){
    frame_init();
    CHECK("baslangicta bos cerceve yok", FRAME_FREE_N == 0 && frame_alloc() == 0);

    frame_range(0x100000ull, 0x100000ull + 16ull * 4096ull, 0);       /* 16 bos cerceve */
    CHECK("16 cerceve acildi", FRAME_FREE_N == 16);

    unsigned long long a = frame_alloc();
    CHECK("ilk ayirma 1MB", a == 0x100000ull);
    CHECK("ayirma sayaci azalir", FRAME_FREE_N == 15);

    frame_range(0x102000ull, 0x104000ull, 1);                         /* 2 cerceve kullanimda */
    CHECK("kullanimdakiler acilmaz", FRAME_FREE_N == 13);

    frame_free(a);
    CHECK("serbest birakma sayaci", FRAME_FREE_N == 14);
    frame_free(a);
    CHECK("cift serbest birakma yok sayilir", FRAME_FREE_N == 14);

    /* kismi cerceve: [0x105001, 0x105002) bir cerceveyi kapsar; kullanimda isaretlenmeli */
    frame_range(0x105001ull, 0x105002ull, 1);
    CHECK("kismi kapsanan cerceve kullanimda", frame_used(0x105000u / 4096u));

    /* tam olmayan bos aralik: kismi cerceveler acilmaz */
    unsigned before = FRAME_FREE_N;
    frame_range(0x200001ull, 0x202000ull, 0);
    CHECK("bos aralikta kismi cerceve acilmaz", FRAME_FREE_N == before + 1);

    frame_init();
    CHECK("frame_init sifirlar", FRAME_FREE_N == 0);
}

int main(void){
    frame_tests();
    heap_tests();
    printf("\n%d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
