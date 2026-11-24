/* WindOS kullanici kutuphanesi (mini libc). Programlar -nostdlib ile derlenir; bu kutuphane
   ihtiyac duyulan kucuk bir alt kumeyi saglar. Sistem cagrilari int 0x80 ile yapilir (userabi.h).
   Kullanim: program dosyasinda #include "../ulib/ulib.h" ve void _start(void) giris noktasi;
   program ul_exit ile bitmelidir. */
#ifndef WIND_ULIB_H
#define WIND_ULIB_H

/* Bellek */
void *memcpy(void *dst, const void *src, unsigned int n);
void *memmove(void *dst, const void *src, unsigned int n);
void *memset(void *dst, int c, unsigned int n);
void *malloc(unsigned int n);
void  free(void *p);

/* Metin */
unsigned int strlen(const char *s);
int          strcmp(const char *a, const char *b);
char        *strcpy(char *dst, const char *src);

/* Sistem */
void          ul_exit(unsigned int code);          /* donmez */
unsigned int  ul_ticks(void);                      /* zamanlayici tick sayisi */
int           ul_write(const void *buf, unsigned int n);   /* standart cikti */
int           ul_printf(const char *fmt, ...);     /* %s %d %u %x %c %% destekli */

#endif
