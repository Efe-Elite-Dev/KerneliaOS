/* WindOS .wind program formati (1. asama: ayristirici).

   Duz, little-endian bir dosya. Basligi 20 bayt:
     0  "WIND"       sihirli sayi
     4  u16 version  = 1
     6  u16 flags    = 0 (ayrilmis)
     8  u32 entry    kod icindeki giris noktasi (0 .. code_size-1)
    12  u32 code_size baslikdan sonraki kod boyutu
    16  u32 mem_size  calisma icin gereken toplam bellek (code_size .. WIND_MAX_MEM)
   Basliktan sonra code_size bayt makine kodu gelir.

   Calistirma (kernel tarafi, sonraki asama): kod mem_size'lik bir bolgeye
   kopyalanir, entry'den baslar; kerneli cagirmak icin bir API tablosu kullanilir. */
#ifndef WIND_H
#define WIND_H

#define WIND_HDR_SIZE 20u
#define WIND_MAX_MEM  (1u << 20)   /* 1 MB ust sinir */

enum {
    WIND_OK = 0,
    WIND_E_SHORT = -1,    /* dosya basligi icin cok kisa */
    WIND_E_MAGIC = -2,    /* "WIND" degil */
    WIND_E_VERSION = -3,  /* desteklenmeyen surum ya da flag */
    WIND_E_SIZE = -4,     /* kod boyutu dosya icinde yok ya da 0 */
    WIND_E_ENTRY = -5,    /* giris noktasi kodun disinda */
    WIND_E_MEM = -6       /* bellek boyutu tutarsiz ya da fazla buyuk */
};

typedef struct {
    unsigned entry;
    unsigned code_size;
    unsigned mem_size;
    const unsigned char *code;   /* dosya tamponunun icinde kodun baslangici */
} wind_prog;

/* img[0..n) bir .wind dosyasi. Gecerliyse out'u doldurur ve WIND_OK doner */
int wind_parse(const unsigned char *img, unsigned n, wind_prog *out);

#endif
