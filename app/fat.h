/* KerneliaOS FAT12/FAT16 surucu (MBR'siz ham birim). libc gerektirmez.
   Disk erisimi geri cagirimlarla yapilir; boylece ATA surucusu, RAM disk ya da
   host dosyasi ayni kodla kullanilabilir.

   Yol bicimi: "HELLO.TXT", "SUB/INNER.TXT", "Uzun Ad.wind" (buyuk/kucuk harf farketmez).
   Uzun dosya adlari (LFN) okunur ve yazilir; en fazla 255 karakter (ASCII).
   Yazma yalnizca fat_set_writer ile yazilabilir birim verildiginde calisir. */
#ifndef FAT_H
#define FAT_H

#define FAT_NAME_MAX 255        /* uzun ad en fazla karakter sayisi */

/* lba'dan itibaren count sektor oku, buf'a yaz. Donus: 0 basarili, -1 hata */
typedef int (*fat_read_fn)(void *ctx, unsigned lba, unsigned count, unsigned char *buf);
/* lba'dan itibaren count sektoru buf'tan yaz. Donus: 0 basarili, -1 hata */
typedef int (*fat_write_fn)(void *ctx, unsigned lba, unsigned count, const unsigned char *buf);

typedef struct {
    fat_read_fn rd;
    fat_write_fn wr;          /* NULL ise birim salt okunur */
    void *ctx;
    int type;                 /* 12 veya 16 */
    unsigned bps, spc;        /* sektor boyutu, kume basina sektor */
    unsigned nfats;           /* FAT kopya sayisi */
    unsigned fat_start;       /* ilk FAT sektoru */
    unsigned fat_size;        /* bir FAT'in sektor sayisi */
    unsigned root_start;      /* kok dizin sektoru (FAT16) */
    unsigned root_sectors;    /* kok dizin sektor sayisi */
    unsigned data_start;      /* ilk veri sektoru (kume 2) */
    unsigned clusters;        /* veri kume sayisi */
    unsigned cache_sec;       /* FAT onbellegi: sektor numarasi (0xFFFFFFFF = bos) */
    unsigned char cache[512];
} fat_vol;

typedef struct {
    unsigned first;           /* ilk kume (0 = kok dizin) */
    unsigned size;            /* dosya boyutu (bayt) */
    unsigned pos;             /* okuma konumu (bayt) */
    unsigned cur;             /* su anki kume */
    unsigned is_dir;
} fat_file;

/* Birimi ac; donus 0 basarili, -1 gecersiz FAT */
int fat_mount(fat_vol *v, fat_read_fn rd, void *ctx);
/* Yazma icin geri cagirim ver (fat_mount'tan sonra). Ayni ctx kullanilir. */
void fat_set_writer(fat_vol *v, fat_write_fn wr);
/* Yolu ac (dosya ya da dizin); donus 0 basarili, -1 bulunamadi */
int fat_open(fat_vol *v, const char *path, fat_file *f);
/* Siradaki n baytı oku; donus okunan bayt sayisi (0 = dosya sonu), -1 hata */
int fat_read(fat_vol *v, fat_file *f, unsigned char *buf, unsigned n);
/* Yeni dosya olustur: path "Dizin/Ad.uzanti" (ust dizin var olmali), size bayt veri yazar.
   Ad 8.3'e uymuyorsa uzun ad (LFN) olarak yazilir. Donus 0 basarili; -1: yazma yok,
   ad zaten var, ad gecersiz, ust dizin yok, yer yok ya da G/C hatasi. */
int fat_create(fat_vol *v, const char *path, const unsigned char *data, unsigned size);
/* Klasor olustur: path "Dizin/Ad" (ust dizin var olmali). Yeni klasorde "." ve ".." girisleri olur.
   Donus 0 basarili; -1: yazma yok, ad zaten var, ad gecersiz, ust dizin yok ya da yer yok. */
int fat_mkdir(fat_vol *v, const char *path);

/* Listeleme geri cagirimi: klasordeki her gercek girdi icin bir kez cagrilir.
   name: uzun ad (LFN varsa) ya da 8.3 adi; is_dir: klasor mu; size: bayt (klasorde 0). */
typedef void (*fat_list_fn)(void *ctx, const char *name, unsigned is_dir, unsigned size);
/* path klasorunu listeler ("" ya da "/" kok dizin). "." ve ".." atlanir. Donus 0 basarili, -1 yok/klasor degil */
int fat_list(fat_vol *v, const char *path, fat_list_fn cb, void *ctx);

#endif
