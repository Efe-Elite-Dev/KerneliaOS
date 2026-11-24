/* WindOS kullanici alani sozlesmesi: cekirdek (usermode.c) ve kullanici kutuphanesi (ulib)
   ortak kullanir. Sistem cagrisi: eax = numara; arglar ebx, ecx, edx, esi, edi; donus eax.
   Kernel32 importlari bu cagrilara bagli stub'lar uzerinden gider (bkz. usermode.c). */
#ifndef WIND_USERABI_H
#define WIND_USERABI_H

/* Kullanici sanal adres haritasi (hepsi cekirdek tablolarinin disinda) */
#define UT_CODE       0x40000000u   /* PE imaji (linker'in ImageBase'i ne olursa olsun buraya relocate edilir) */
#define UT_STUB       0x3FF00000u   /* kernel32 import stub'lari (4 KB) */
#define UT_HEAP       0x50000000u   /* kullanici heap'i */
#define UT_HEAP_SIZE  (256u * 1024u)
#define UT_STACK_TOP  0x80000000u   /* yigin: [UT_STACK_TOP - 64 KB, UT_STACK_TOP) */
#define UT_LO         0x40000000u   /* kullanici VA alt siniri (dahil) */
#define UT_HI         0x80000000u   /* kullanici VA ust siniri (dahil degil) */

/* Sistem cagri numaralari */
#define SYS_EXIT          1         /* ebx = cikis kodu; donmez */
#define SYS_GETSTDHANDLE  2         /* ebx = std id; donus: tutamac */
#define SYS_WRITE         3         /* ebx = tutamac, ecx = tampon, edx = uzunluk, esi = yazilan (ya da 0) */
#define SYS_TICKS         4         /* donus: zamanlayici tick sayisi */

#endif
