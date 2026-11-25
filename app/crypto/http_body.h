/* KerneliaOS HTTP/1.1 yanit yardimcilari: durum kodu, govde baslangici, chunked cozme.
   Bellek kopyalamasi yok: giris dizisi dogrudan okunur; cozme ciktisi cagirana ait tampona yazilir. */
#ifndef KERNELIA_HTTP_BODY_H
#define KERNELIA_HTTP_BODY_H
#include <stdint.h>
#include <stddef.h>

/* "HTTP/1.x NNN" satirindan durum kodu. Ayristirilamazsa -1. */
int http_status(const uint8_t *resp, size_t n);

/* Baslik sonunun (CRLFCRLF) hemen sonraki govde konumu. Yoksa -1. */
long http_body_offset(const uint8_t *resp, size_t n);

/* Basliklarda "Transfer-Encoding: chunked" var mi (buyuk-kucuk harf fark etmez). */
int http_is_chunked(const uint8_t *resp, size_t n);

/* Chunked govdeyi out'a cozer (in: govde, yalniz baslik sonrasi).
   Donus: cozulen bayt sayisi (>= 0); -1 bozuk kodlama; -2 veri eksik (yanit kesilmis);
   -3 cikti tamponu yetmiyor. Sonlandirici 0-chunk ve trailer'lar okunur. */
long http_dechunk(const uint8_t *in, size_t n, uint8_t *out, size_t cap);

#endif
