/* KerneliaOS tarayici - HTML ayristirici (1. asama).
   libc gerektirmez: kernel'e de, host uzerindeki test programina da baglanabilir.

   Cikti: duz metin + parcalar (run). Her parca bir stil tasir (normal, baslik
   h1..h6, baglanti) ve baglantiysa adresini (href) tasir. Parcadan once satir
   ya da paragraf kesmesi gerekiyorsa brk ile belirtilir. */
#ifndef HTML_LITE_H
#define HTML_LITE_H

enum { HL_STYLE_NORMAL = 0, HL_STYLE_LINK = 10 };   /* 1..6: basliklar h1..h6 */
enum { HL_BRK_NONE = 0, HL_BRK_LINE = 1, HL_BRK_PARA = 2 };

typedef struct {
    int off, len;           /* metin: text[off .. off+len) */
    int style;              /* HL_STYLE_NORMAL, 1..6 (basliklar), HL_STYLE_LINK */
    int brk;                /* bu parcadan once: HL_BRK_* */
    int href_off, href_len; /* baglanti adresi: href[href_off .. +href_len); 0 = yok */
} hl_run;

typedef struct {
    char *text;   int text_cap, text_len;
    char *href;   int href_cap, href_len;
    hl_run *runs; int run_cap, run_n;
} hl_doc;

/* Donus: 0 tamam, 1 tampon dolu, cikti kesildi. Girdi html[0..n) */
int hl_parse(hl_doc *d, const char *html, int n);

#endif
