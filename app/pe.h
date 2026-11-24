/* KerneliaOS PE32 (i386) yukleyici. Bkz. pe.c
   Kapsam: MZ/PE basligi, bolumler, base relocation (HIGHLOW), import tablosu.
   Kernel bagimliligi yok; import cozumu bir geri cagirimla yapilir. */
#ifndef PE_H
#define PE_H

enum {
    PE_OK = 0,
    PE_E_SHORT = -1,     /* dosya cok kisa */
    PE_E_MZ = -2,        /* "MZ" yok */
    PE_E_PE = -3,        /* "PE\0\0" yok */
    PE_E_MACHINE = -4,   /* i386 degil ya da PE32 degil */
    PE_E_RANGE = -5,     /* bolum/dizin dosya ya da bellek disina tasiyor */
    PE_E_SIZE = -6,      /* image boyutu hedef tampondan buyuk */
    PE_E_RELOC = -7,     /* desteklenmeyen relocation tipi */
    PE_E_IMPORT = -8     /* import cozulemedi */
};

/* Import cozucu: dll/ad ile fonksiyon adresi ister. ad NULL ise ordinal (ord) kullanilir.
   Donus 0: bulunamadi (yukleme durur). */
typedef unsigned (*pe_resolve_fn)(void *ctx, const char *dll, const char *name, unsigned ord);

typedef struct {
    unsigned entry_rva;       /* giris noktasi (RVA) */
    unsigned image_base;      /* linker'in tercih ettigi adres */
    unsigned image_size;      /* bellekteki toplam boyut */
    unsigned nsections;
    unsigned nimports;        /* cozulen import sayisi */
    unsigned nrelocs;         /* uygulanan relocation sayisi */
} pe_info;

/* img[0..n) bir PE dosyasi: basligi dogrular ve out'u doldurur */
int pe_parse(const unsigned char *img, unsigned n, pe_info *out);

/* Dosyayi dst'ye (dst_size >= image_size) yerlestirir. load_addr: dst'nin
   programin gorecegi sanal adresi (relocation buna gore yapilir).
   Import'lar res ile cozulur. entry_out'a mutlak giris adresi (load_addr + entry_rva) yazilir. */
int pe_load(const unsigned char *img, unsigned n,
            unsigned char *dst, unsigned dst_size, unsigned load_addr,
            pe_resolve_fn res, void *ctx, pe_info *out, unsigned *entry_out);

/* Hata kodunun Turkce metni (terminal icin) */
const char *pe_err_text(int e);

#endif
