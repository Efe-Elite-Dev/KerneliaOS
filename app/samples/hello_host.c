/* hello.c mantiginin host testi: sahte bir api ile calistirir ve cagri dizisini kontrol eder */
#include <stdio.h>
#include <string.h>
#include "../wind.h"

static char texts[4][16];
static int ntexts = 0, presents = 0, keys_asked = 0;

static void fill(int x, int y, int w, int h, unsigned c){ (void)x; (void)y; (void)w; (void)h; (void)c; }
static void pixel(int x, int y, unsigned c){ (void)x; (void)y; (void)c; }
static void text(int x, int y, const char *s, unsigned c, int sc){
    (void)x; (void)y; (void)c; (void)sc;
    if(ntexts < 4){ strncpy(texts[ntexts], s, 15); texts[ntexts][15] = 0; }
    ntexts++;
}
static void present(void){ presents++; }
static int key(void){ keys_asked++; return keys_asked >= 3 ? 27 : 0; }
static void wait_(void){ }
static unsigned ticks(void){ return 0; }

/* hello.c icindeki entry (host derlemesinde normal fonksiyon) */
void entry(const wind_api *api);

int main(void){
    wind_api api = { 1, fill, pixel, text, present, key, wait_, ticks, 0, 0 };
    entry(&api);
    int ok = ntexts == 2 && strcmp(texts[0], "MERHABA WIN") == 0 && strcmp(texts[1], "TUS CIK") == 0
         && presents == 1 && keys_asked == 3;
    printf("%s: text=%d present=%d keys=%d\n", ok ? "ok   hello.c mantigi" : "FAIL hello.c mantigi",
           ntexts, presents, keys_asked);
    return ok ? 0 : 1;
}
