/* fat.c icin host testi. Once imajlari uret:
     python3 app/test/make_fat_images.py <klasor>
   Sonra: test_fat <klasor>/fat16.img <klasor>/fat12.img [kayit16.img kayit12.img]
   Yazma testleri imajin gecici bir kopyasinda calisir; gercek dosya degismez.
   Son iki ad verilirse klasor testinin sonundaki kopyalar oraya kaydedilir (fsck.fat -n ile denetlenir). */
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

/* gecici kopyayi dst'ye kaydeder (fsck.fat ile denetlemek icin) */
static int save_copy(FILE *t, const char *dst){
    unsigned char blk[4096];
    size_t n;
    FILE *o = fopen(dst, "wb");
    if(!o) return -1;
    fflush(t);
    if(fseek(t, 0, SEEK_SET) != 0){ fclose(o); return -1; }
    while((n = fread(blk, 1, sizeof blk, t)) > 0){
        if(fwrite(blk, 1, n, o) != n){ fclose(o); return -1; }
    }
    fclose(o);
    return 0;
}

/* klasor listesi: fat_list geri cagirimiyla doldurulur */
typedef struct {
    char name[128][FAT_NAME_MAX + 1];
    unsigned is_dir[128], size[128];
    int n, over;
} list_t;

static void list_cb(void *ctx, const char *name, unsigned is_dir, unsigned size){
    list_t *l = (list_t *)ctx;
    unsigned k = 0;
    if(l->n >= 128){ l->over = 1; return; }
    while(k < FAT_NAME_MAX && name[k]){ l->name[l->n][k] = name[k]; k++; }
    l->name[l->n][k] = 0;
    l->is_dir[l->n] = is_dir;
    l->size[l->n] = size;
    l->n++;
}

static int list_find(const list_t *l, const char *name){
    for(int i = 0; i < l->n; i++) if(strcmp(l->name[i], name) == 0) return i;
    return -1;
}

/* ad listede mi? Klasorse is_dir=1 (boyut bakilmaz); dosyaysa boyut da tutmali */
static int list_is(const list_t *l, const char *name, unsigned is_dir, unsigned size){
    int i = list_find(l, name);
    return i >= 0 && l->is_dir[i] == is_dir && (is_dir || l->size[i] == size);
}

/* klasor olusturma, ic ice klasor, dizin buyumesi, listeleme ve kalicilik */
static void dir_tests(const char *path, int want_type, unsigned hello_size, const char *keep){
    static unsigned char pat[3000];
    static list_t L;
    FILE *t = temp_copy(path);
    img_t img;
    fat_vol v;
    char p[96], msg[200];
    int ok_all = 1;
    if(!t){ printf("FAIL klasor testi icin gecici kopya: %s\n", path); fails++; return; }
    img.f = t;
    for(unsigned i = 0; i < sizeof pat; i++) pat[i] = (unsigned char)((i * 13) & 0xFF);

    snprintf(msg, sizeof msg, "%s: klasor testi icin baglandi (FAT%d)", path, want_type);
    check(msg, fat_mount(&v, img_read, &img) == 0 && v.type == want_type);
    fat_set_writer(&v, img_write);

    check("klasor olusturuldu", fat_mkdir(&v, "Yeni Klasor") == 0);
    check("ayni klasor buyuk harfle reddedilir", fat_mkdir(&v, "YENI KLASOR") == -1);
    check("klasor adiyla ayni ad dosya olarak reddedilir", fat_create(&v, "Yeni Klasor", pat, 1) == -1);
    check("gecersiz klasor adi reddedilir", fat_mkdir(&v, "bad*dir") == -1);
    check("olmayan ust klasorde klasor reddedilir", fat_mkdir(&v, "NoSuch/Alt") == -1);
    check("dosyanin icinde klasor reddedilir", fat_mkdir(&v, "HELLO.TXT/Alt") == -1);
    check("ic ice klasor olusturuldu", fat_mkdir(&v, "Yeni Klasor/Alt Klasor") == 0);
    check("klasor icine LFN dosya", fat_create(&v, "Yeni Klasor/Icerik Dosyasi.txt", (const unsigned char *)"ICERIK", 6) == 0);
    check("ic ice klasore kucuk harfli dosya", fat_create(&v, "Yeni Klasor/Alt Klasor/derin.txt", (const unsigned char *)"DERIN2", 6) == 0);
    check("klasor icindeki dosya okundu (kucuk harf yol)", file_is(&v, "yeni klasor/icerik dosyasi.txt", (const unsigned char *)"ICERIK", 6));
    check("ic ice dosya okundu (buyuk harf yol)", file_is(&v, "Yeni Klasor/Alt Klasor/DERIN.TXT", (const unsigned char *)"DERIN2", 6));

    /* dizin buyumesi: 20 dosya tek kumeye sigmaz; hepsinin takma adi ayni on ekle baslar */
    for(int i = 0; i < 20; i++){
        snprintf(p, sizeof p, "Yeni Klasor/Kayit %02d.txt", i);
        if(fat_create(&v, p, pat, 100 + (unsigned)i) != 0) ok_all = 0;
    }
    check("klasor 20 dosyayla buyudu (ortak takma ad oneki)", ok_all);

    L.n = 0; L.over = 0;
    check("kok dizin listelendi", fat_list(&v, "", list_cb, &L) == 0);
    check("kok dizin: klasorler dizin olarak gorunur",
          list_is(&L, "Yeni Klasor", 1, 0) && list_is(&L, "SUB", 1, 0) && list_is(&L, "Long Dir Name", 1, 0));
    check("kok dizin: 8.3 dosya boyutuyla", list_is(&L, "HELLO.TXT", 0, hello_size));
    check("kok dizin: LFN dosyalar boyutuyla", list_is(&L, "HELLO.WIND", 0, 1500) && list_is(&L, "Long Name Sample.txt", 0, 7));
    check("kok dizin: takma ad 8.3 olarak gorunur", list_is(&L, "STALEN~1.TXT", 0, 6));
    {
        static char long120[160], long255[260];
        memset(long120, 'A', 120);
        strcpy(long120 + 120, ".txt");
        memset(long255, 'C', 255);
        long255[255] = 0;
        check("kok dizin: 124 karakterli ad", list_is(&L, long120, 0, 7));
        check("kok dizin: 255 karakterli ad", list_is(&L, long255, 0, 7));
    }
    check("kok dizin: olmayan ad yok", list_find(&L, "NOPE.TXT") < 0);
    check("dosya klasor gibi listelenmez", fat_list(&v, "HELLO.TXT", list_cb, &L) == -1);
    check("olmayan klasor listelenmez", fat_list(&v, "NoSuch", list_cb, &L) == -1);

    L.n = 0; L.over = 0;
    check("yeni klasor listelendi", fat_list(&v, "Yeni Klasor", list_cb, &L) == 0);
    check("yeni klasor: 22 girdi, . ve .. yok", L.n == 22 && !L.over && list_find(&L, ".") < 0 && list_find(&L, "..") < 0);
    check("yeni klasor: LFN dosya boyutuyla", list_is(&L, "Icerik Dosyasi.txt", 0, 6));
    check("yeni klasor: alt klasor dizin olarak", list_is(&L, "Alt Klasor", 1, 0));
    ok_all = 1;
    for(int i = 0; i < 20; i++){
        snprintf(p, sizeof p, "Kayit %02d.txt", i);
        if(!list_is(&L, p, 0, 100 + (unsigned)i)) ok_all = 0;
    }
    check("yeni klasor: 20 dosyanin hepsi boyutuyla listelendi", ok_all);

    L.n = 0; L.over = 0;
    check("ic ice klasor listelendi", fat_list(&v, "Yeni Klasor/Alt Klasor", list_cb, &L) == 0);
    check("ic ice klasor: derin.txt (LFN, kucuk harf)", L.n == 1 && list_is(&L, "derin.txt", 0, 6));

    /* yeniden bagla: yazilanlar diskte kalici mi? */
    fflush(t);
    check("klasor testinden sonra yeniden baglandi", fat_mount(&v, img_read, &img) == 0 && v.type == want_type);
    L.n = 0; L.over = 0;
    check("kalici: kok dizinde yeni klasor", fat_list(&v, "", list_cb, &L) == 0 && list_is(&L, "Yeni Klasor", 1, 0));
    check("kalici: ic ice dosya icerigi", file_is(&v, "Yeni Klasor/Alt Klasor/derin.txt", (const unsigned char *)"DERIN2", 6));
    check("kalici: buyuyen klasordeki son dosya", file_is(&v, "Yeni Klasor/Kayit 19.txt", pat, 119));
    check("kalici: eski alt klasor dosyasi bozulmadi", file_is(&v, "SUB/INNER.TXT", (const unsigned char *)"ICERIDEN", 8));

    if(keep){
        if(save_copy(t, keep) == 0) printf("ok   klasor testi imaji kaydedildi: %s\n", keep);
        else { printf("FAIL klasor testi imaji kaydedilemedi: %s\n", keep); fails++; }
    }
    fclose(t);
}

int main(int argc, char **argv){
    if(argc < 3){ printf("kullanim: test_fat fat16.img fat12.img [kayit16.img kayit12.img]\n"); return 2; }
    run_image(argv[1], 16, 5000);
    run_image(argv[2], 12, 3000);
    write_tests(argv[1], 16, 5000);
    write_tests(argv[2], 12, 3000);
    dir_tests(argv[1], 16, 5000, argc > 3 ? argv[3] : NULL);
    dir_tests(argv[2], 12, 3000, argc > 4 ? argv[4] : NULL);

    {
        fat_vol v;
        check("bozuk sektor 0 reddedilir", fat_mount(&v, zero_read, 0) == -1);
    }

    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
