/* fat.c icin host testi. Once imajlari uret:
     python3 app/test/make_fat_images.py <klasor>
   Sonra: test_fat <klasor>/fat16.img <klasor>/fat12.img */
#include <stdio.h>
#include <string.h>
#include "../fat.h"

typedef struct { FILE *f; } img_t;
static int fails = 0;

static int img_read(void *ctx, unsigned lba, unsigned count, unsigned char *buf){
    FILE *f = ((img_t *)ctx)->f;
    if(fseek(f, (long)lba * 512L, SEEK_SET) != 0) return -1;
    if(fread(buf, 512, count, f) != count) return -1;
    return 0;
}

static int zero_read(void *ctx, unsigned lba, unsigned count, unsigned char *buf){
    (void)ctx; (void)lba;
    memset(buf, 0, (size_t)count * 512);
    return 0;
}

static void check(const char *name, int ok){
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if(!ok) fails++;
}

/* dosyayi chunk'larla bbastan sona oku; okunan toplam bayt sayisini doner */
static int read_all(fat_vol *v, fat_file *f, unsigned char *out, unsigned cap){
    unsigned total = 0;
    for(;;){
        unsigned want = (cap - total) < 333 ? (cap - total) : 333;
        if(want == 0) break;
        int r = fat_read(v, f, out + total, want);
        if(r < 0) return -1;
        if(r == 0) break;
        total += (unsigned)r;
    }
    return (int)total;
}

static int pattern_ok(const unsigned char *b, unsigned n){
    for(unsigned i = 0; i < n; i++) if(b[i] != (unsigned char)((i * 7) & 0xFF)) return 0;
    return 1;
}

static void run_image(const char *path, int want_type, unsigned hello_size){
    FILE *fp = fopen(path, "rb");
    img_t img;
    fat_vol v;
    fat_file f;
    static unsigned char buf[8192];
    int r;
    char name[160];
    if(!fp){ printf("FAIL goruntu acilamadi: %s\n", path); fails++; return; }
    img.f = fp;

    snprintf(name, sizeof name, "%s: bagla (FAT%d)", path, want_type);
    check(name, fat_mount(&v, img_read, &img) == 0 && v.type == want_type);

    r = fat_open(&v, "HELLO.TXT", &f);
    check("HELLO.TXT acildi", r == 0 && f.size == hello_size && !f.is_dir);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("HELLO.TXT icerigi (kume gecisleri dahil)", n == (int)hello_size && pattern_ok(buf, hello_size));
        check("dosya sonunda 0 bayt", fat_read(&v, &f, buf, 10) == 0);
    }

    r = fat_open(&v, "sub/inner.txt", &f);
    check("kucuk harfle SUB/INNER.TXT acildi", r == 0 && f.size == 8);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("INNER.TXT icerigi", n == 8 && memcmp(buf, "ICERIDEN", 8) == 0);
    }

    r = fat_open(&v, "SUB/DEEP/X.TXT", &f);
    check("SUB/DEEP/X.TXT acildi", r == 0 && f.size == 6);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("X.TXT icerigi (iki seviye)", n == 6 && memcmp(buf, "DERIN!", 6) == 0);
    }

    r = fat_open(&v, "SUB/DEEP/", &f);
    check("sondaki / ile dizin acildi", r == 0 && f.is_dir);

    r = fat_open(&v, "", &f);
    check("kok dizin", r == 0 && f.is_dir);

    check("olmayan dosya", fat_open(&v, "NOPE.TXT", &f) == -1);
    check("olmayan alt dosya", fat_open(&v, "SUB/NOPE.TXT", &f) == -1);
    check("dosya yerine dizin gibi kullanma", fat_open(&v, "HELLO.TXT/X", &f) == -1);

    fclose(fp);
}

int main(int argc, char **argv){
    if(argc < 3){ printf("kullanim: test_fat fat16.img fat12.img\n"); return 2; }
    run_image(argv[1], 16, 5000);
    run_image(argv[2], 12, 3000);

    {
        fat_vol v;
        check("bozuk sektor 0 reddedilir", fat_mount(&v, zero_read, 0) == -1);
    }

    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
