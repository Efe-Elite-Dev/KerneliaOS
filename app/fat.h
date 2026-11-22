/* WindOS FAT12/FAT16 okuyucu (salt okunur, MBR'siz ham birim).
   libc gerektirmez. Disk erisimi bir okuma geri cagirimi ile yapilir; boylece
   ATA surucusu, RAM disk ya da host dosyasi ayni kodla kullanilabilir.

   Yol bicimi: "HELLO.TXT", "SUB/INNER.TXT", "Uzun Ad.wind" (buyuk/kucuk harf farketmez;
   uzun dosya adlari (LFN) desteklenir, en fazla 80 karakter). */
#ifndef FAT_H
#define FAT_H

/* lba'dan itibaren count sektor oku, buf'a yaz. Donus: 0 basarili, -1 hata */
typedef int (*fat_read_fn)(void *ctx, unsigned lba, unsigned count, unsigned char *buf);

typedef struct {
    fat_read_fn rd;
    void *ctx;
    int type;                 /* 12 veya 16 */
    unsigned bps, spc;        /* sektor boyutu, kume basina sektor */
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
/* Yolu ac (dosya ya da dizin); donus 0 basarili, -1 bulunamadi */
int fat_open(fat_vol *v, const char *path, fat_file *f);
/* Siradaki n baytı oku; donus okunan bayt sayisi (0 = dosya sonu), -1 hata */
int fat_read(fat_vol *v, fat_file *f, unsigned char *buf, unsigned n);

#endif
