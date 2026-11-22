/* fat.c icin host testi. Once imajlari uret:
     python3 app/test/make_fat_images.py <klasor>
   Sonra: test_fat <klasor>/fat16.img <klasor>/fat12.img
   Yazma testleri imajin gecici bir kopyasinda calisir; gercek dosya degismez. */
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

static int img_write(void *ctx, unsigned lba, unsigned count, const unsigned char *buf){
    FILE *f = ((img_t *)ctx)->f;
    if(fseek(f, (long)lba * 512L, SEEK_SET) != 0) return -1;
    if(fwrite(buf, 512, count, f) != count) return -1;
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

/* yol n baytlik beklenen veriyle ayni mi? (dosya olmali) */
static int file_is(fat_vol *v, const char *path, const unsigned char *want, unsigned n){
    static unsigned char got[8192];
    fat_file f;
    if(fat_open(v, path, &f) != 0 || f.is_dir || f.size != n) return 0;
    if(read_all(v, &f, got, sizeof got) != (int)n) return 0;
    return memcmp(got, want, n) == 0;
}

/* imajin gecici, okunup yazilabilir bir kopyasi (tmpfile diskten bagimsiz) */
static FILE *temp_copy(const char *path){
    unsigned char blk[4096];
    size_t n;
    FILE *src = fopen(path, "rb");
    FILE *t = tmpfile();
    if(!src || !t){ if(src) fclose(src); if(t) fclose(t); return NULL; }
    while((n = fread(blk, 1, sizeof blk, src)) > 0){
        if(fwrite(blk, 1, n, t) != n){ fclose(src); fclose(t); return NULL; }
    }
    fclose(src);
    fflush(t);
    return t;
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

    /* uzun dosya adlari (LFN) */
    r = fat_open(&v, "HELLO.WIND", &f);
    check("LFN HELLO.WIND acildi (8.3'e sigmayan uzanti)", r == 0 && f.size == 1500 && !f.is_dir);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("HELLO.WIND icerigi", n == 1500 && pattern_ok(buf, 1500));
    }
    r = fat_open(&v, "hello.wind", &f);
    check("LFN kucuk harfle acildi", r == 0 && f.size == 1500);

    r = fat_open(&v, "Long Name Sample.txt", &f);
    check("LFN 2 girisli ad acildi", r == 0 && f.size == 7);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("LFN 2 girisli icerik", n == 7 && memcmp(buf, "UZUN AD", 7) == 0);
    }
    check("LFN buyuk harf farketmez", fat_open(&v, "LONG NAME SAMPLE.TXT", &f) == 0);
    check("LFN eksik harf reddedilir", fat_open(&v, "Long Name Sampl.txt", &f) == -1);
    check("LFN fazla harf reddedilir", fat_open(&v, "Long Name Sample.txtx", &f) == -1);

    r = fat_open(&v, "a very long file name for testing.dat", &f);
    check("LFN 3 girisli ad acildi", r == 0 && f.size == 4);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("LFN 3 girisli icerik", n == 4 && memcmp(buf, "LFN3", 4) == 0);
    }

    /* sagla toplami bozuk zincir: uzun ad bulunamaz, ama kisa ad acilir */
    check("bozuk LFN zinciri eslesmez", fat_open(&v, "Stale Name.txt", &f) == -1);
    r = fat_open(&v, "stalen~1.txt", &f);
    check("bozuk zincirdeki kisa ad acilir", r == 0 && f.size == 6);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("kisa ad icerigi", n == 6 && memcmp(buf, "YANLIS", 6) == 0);
    }

    /* uzun adli dizin ve sinirlar */
    r = fat_open(&v, "Long Dir Name/Inner Long File.txt", &f);
    check("LFN dizin icindeki dosya acildi", r == 0 && f.size == 8);
    if(r == 0){
        int n = read_all(&v, &f, buf, sizeof buf);
        check("LFN dizin icerigi", n == 8 && memcmp(buf, "ICI UZUN", 8) == 0);
    }
    check("LFN dizin kucuk harfle", fat_open(&v, "long dir name/inner long file.txt", &f) == 0);
    r = fat_open(&v, "Long Dir Name/", &f);
    check("LFN dizin acildi", r == 0 && f.is_dir);
    {
        static char longname[300];
        memset(longname, 'A', 120);
        strcpy(longname + 120, ".txt");
        r = fat_open(&v, longname, &f);
        check("120 karakterli ad acildi", r == 0 && f.size == 7);
        if(r == 0){
            int n = read_all(&v, &f, buf, sizeof buf);
            check("120 karakterli ad icerigi", n == 7 && memcmp(buf, "LONG120", 7) == 0);
        }
        memset(longname, 'C', 255);
        longname[255] = 0;
        r = fat_open(&v, longname, &f);
        check("255 karakterli ad acildi", r == 0 && f.size == 7);
        longname[255] = 'C';
        longname[256] = 0;
        check("256 karakterli ad reddedilir", fat_open(&v, longname, &f) == -1);
    }

    fclose(fp);
}

/* yazma: dosya olusturma, LFN, takma ad cakismasi, dizin buyumesi, kaliciligi */
static void write_tests(const char *path, int want_type, unsigned hello_size){
    static const char *names[] = {
        "Alpha Long Name.txt", "Beta Long Name.txt", "Gamma Long Name.txt", "Delta Long Name.txt",
        "Epsilon Long Name.txt", "Zeta Long Name.txt", "Eta Long Name.txt", "Theta Long Name.txt",
        "Iota Long Name.txt", "Kappa Long Name.txt", "Lambda Long Name.txt", "Mu Long Name.txt"
    };
    static unsigned char pat[7000];
    static char big[300];
    FILE *t = temp_copy(path);
    img_t img;
    fat_vol v;
    char msg[200];
    int ok_all;
    if(!t){ printf("FAIL yazma testi icin gecici kopya: %s\n", path); fails++; return; }
    img.f = t;
    for(unsigned i = 0; i < sizeof pat; i++) pat[i] = (unsigned char)((i * 7) & 0xFF);

    snprintf(msg, sizeof msg, "%s: yazma icin baglandi (FAT%d)", path, want_type);
    check(msg, fat_mount(&v, img_read, &img) == 0 && v.type == want_type);
    check("yazici yokken olusturma reddedilir", fat_create(&v, "X.TXT", pat, 1) == -1);
    fat_set_writer(&v, img_write);

    check("LFN dosya olusturuldu (7000 bayt, birden fazla kume)",
          fat_create(&v, "New Long Name File.txt", pat, 7000) == 0);
    check("ayni ad (buyuk harf) ikinci kez reddedilir",
          fat_create(&v, "NEW LONG NAME FILE.TXT", pat, 1) == -1);
    check("gecersiz karakter reddedilir", fat_create(&v, "bad*name.txt", pat, 1) == -1);
    check("olmayan ust dizin reddedilir", fat_create(&v, "NoSuchDir/x.txt", pat, 1) == -1);
    memset(big, 'D', 256);
    big[256] = 0;
    check("256 karakterli ad reddedilir", fat_create(&v, big, pat, 1) == -1);
    memset(big, 'E', 255);
    big[255] = 0;
    check("255 karakterli ad olusturuldu", fat_create(&v, big, (const unsigned char *)"ok", 2) == 0);
    check("8.3 ad dogrudan olusturuldu", fat_create(&v, "SMALL.TXT", (const unsigned char *)"abc", 3) == 0);
    check("kucuk harfli 8.3 ad (LFN) olusturuldu", fat_create(&v, "tiny.txt", (const unsigned char *)"xyz", 3) == 0);
    check("ayni on ekli iki LFN ad (takma ad cakismasi)",
          fat_create(&v, "Long Name Sample 2.txt", (const unsigned char *)"ALIAS2", 6) == 0);
    check("LFN dizine dosya olusturuldu",
          fat_create(&v, "Long Dir Name/Yeni Dosya Ekle.txt", pat, 3000) == 0);

    /* dizin buyumesi: LFN dizin tek kumeye sigmaz (fat12: 16 giris/kume) */
    ok_all = 1;
    for(unsigned i = 0; i < sizeof names / sizeof names[0]; i++){
        char p[80];
        snprintf(p, sizeof p, "Long Dir Name/%s", names[i]);
        if(fat_create(&v, p, (const unsigned char *)names[i], (unsigned)strlen(names[i])) != 0) ok_all = 0;
    }
    check("12 dosya LFN dizinde (dizin buyumesi)", ok_all);

    /* yeniden bagla: yazilanlar diskte kalici mi? */
    fflush(t);
    check("yeniden baglandi", fat_mount(&v, img_read, &img) == 0 && v.type == want_type);
    check("yazilan LFN dosya okundu", file_is(&v, "new long name file.txt", pat, 7000));
    check("eski LFN dosya bozulmadi", file_is(&v, "Long Name Sample.txt", (const unsigned char *)"UZUN AD", 7));
    check("cakisan ad yeni dosya", file_is(&v, "Long Name Sample 2.txt", (const unsigned char *)"ALIAS2", 6));
    check("8.3 ad okundu", file_is(&v, "SMALL.TXT", (const unsigned char *)"abc", 3));
    check("kucuk harfli ad okundu", file_is(&v, "TINY.TXT", (const unsigned char *)"xyz", 3));
    {
        fat_file f2;
        check("255 karakterli yazilan ad okundu", fat_open(&v, big, &f2) == 0 && f2.size == 2);
    }
    check("LFN dizindeki yeni dosya", file_is(&v, "Long Dir Name/Yeni Dosya Ekle.txt", pat, 3000));
    check("dizin eski dosyasi bozulmadi", file_is(&v, "Long Dir Name/Inner Long File.txt", (const unsigned char *)"ICI UZUN", 8));
    ok_all = 1;
    for(unsigned i = 0; i < sizeof names / sizeof names[0]; i++){
        char p[80];
        snprintf(p, sizeof p, "Long Dir Name/%s", names[i]);
        if(!file_is(&v, p, (const unsigned char *)names[i], (unsigned)strlen(names[i]))) ok_all = 0;
    }
    check("buyuyen dizindeki 12 dosyanin hepsi okundu", ok_all);
    check("kok dizin HELLO.TXT bozulmadi", file_is(&v, "HELLO.TXT", pat, hello_size));

    fclose(t);
}

int main(int argc, char **argv){
    if(argc < 3){ printf("kullanim: test_fat fat16.img fat12.img\n"); return 2; }
    run_image(argv[1], 16, 5000);
    run_image(argv[2], 12, 3000);
    write_tests(argv[1], 16, 5000);
    write_tests(argv[2], 12, 3000);

    {
        fat_vol v;
        check("bozuk sektor 0 reddedilir", fat_mount(&v, zero_read, 0) == -1);
    }

    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
