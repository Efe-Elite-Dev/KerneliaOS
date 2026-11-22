/* wind.c icin host testi: gecerli ve bozuk basliklar */
#include <stdio.h>
#include <string.h>
#include "../wind.h"

static int fails = 0;

static void put32(unsigned char *p, unsigned v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void check(const char *name, int got, int want){
    if(got != want){ printf("FAIL %s: got %d want %d\n", name, got, want); fails++; }
    else printf("ok   %s\n", name);
}

/* gecerli bir 16 baytlik kodlu dosya uretir */
static unsigned make_valid(unsigned char *img){
    memcpy(img, "WIND", 4);
    img[4] = 1; img[5] = 0; img[6] = 0; img[7] = 0;
    put32(img + 8, 4);      /* entry */
    put32(img + 12, 16);    /* code_size */
    put32(img + 16, 4096);  /* mem_size */
    memset(img + WIND_HDR_SIZE, 0x90, 16);
    return WIND_HDR_SIZE + 16;
}

int main(void){
    unsigned char img[64];
    wind_prog p;
    unsigned n = make_valid(img);

    check("gecerli dosya", wind_parse(img, n, &p), WIND_OK);
    check("giris noktasi okundu", p.entry == 4 && p.code_size == 16 && p.mem_size == 4096, 1);
    check("kod isaretcisi basliktan sonra", p.code == img + WIND_HDR_SIZE, 1);

    check("bos dosya", wind_parse(img, 5, &p), WIND_E_SHORT);

    img[0] = 'X';
    check("yanlis sihirli sayi", wind_parse(img, n, &p), WIND_E_MAGIC);
    img[0] = 'W';

    img[4] = 2;
    check("desteklenmeyen surum", wind_parse(img, n, &p), WIND_E_VERSION);
    img[4] = 1;

    img[6] = 1;
    check("ayrilmis flag", wind_parse(img, n, &p), WIND_E_VERSION);
    img[6] = 0;

    put32(img + 12, 0);
    check("kod boyutu 0", wind_parse(img, n, &p), WIND_E_SIZE);
    put32(img + 12, 1000);
    check("kod boyutu dosyadan buyuk", wind_parse(img, n, &p), WIND_E_SIZE);
    put32(img + 12, 16);

    put32(img + 8, 16);
    check("giris kodun disinda", wind_parse(img, n, &p), WIND_E_ENTRY);
    put32(img + 8, 4);

    put32(img + 16, 8);
    check("bellek kod boyutundan kucuk", wind_parse(img, n, &p), WIND_E_MEM);
    put32(img + 16, WIND_MAX_MEM + 1);
    check("bellek ust siniri asildi", wind_parse(img, n, &p), WIND_E_MEM);
    put32(img + 16, 4096);

    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
