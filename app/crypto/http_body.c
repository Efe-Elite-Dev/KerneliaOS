/* KerneliaOS HTTP/1.1 yanit yardimcilari. Bkz. http_body.h */
#include "http_body.h"

static int hb_lower(int c){ return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* haystack[i..] icinde needle (kucuk harf karsilastirma) var mi */
static int hb_match_ci(const uint8_t *h, size_t hn, size_t i, const char *needle){
    size_t k = 0;
    for(; needle[k]; k++){
        if(i + k >= hn) return 0;
        if(hb_lower(h[i + k]) != hb_lower((uint8_t)needle[k])) return 0;
    }
    return 1;
}

int http_status(const uint8_t *resp, size_t n){
    /* "HTTP/" + surum + " " + 3 hane */
    if(n < 12 || resp[0] != 'H' || resp[1] != 'T' || resp[2] != 'T' || resp[3] != 'P' || resp[4] != '/') return -1;
    size_t i = 5;
    while(i < n && resp[i] != ' ') i++;
    if(i + 4 > n || resp[i] != ' ') return -1;
    int code = 0;
    for(size_t k = 1; k <= 3; k++){
        uint8_t c = resp[i + k];
        if(c < '0' || c > '9') return -1;
        code = code * 10 + (c - '0');
    }
    return code;
}

long http_body_offset(const uint8_t *resp, size_t n){
    for(size_t i = 0; i + 3 < n; i++){
        if(resp[i] == '\r' && resp[i + 1] == '\n' && resp[i + 2] == '\r' && resp[i + 3] == '\n')
            return (long)(i + 4);
    }
    return -1;
}

int http_is_chunked(const uint8_t *resp, size_t n){
    long body = http_body_offset(resp, n);
    size_t hn = (body < 0) ? n : (size_t)body;
    static const char key[] = "transfer-encoding:";
    static const char val[] = "chunked";
    for(size_t i = 0; i < hn; i++){
        if(!hb_match_ci(resp, hn, i, key)) continue;
        size_t j = i + (sizeof key - 1);
        while(j < hn && (resp[j] == ' ' || resp[j] == '\t')) j++;
        if(hb_match_ci(resp, hn, j, val)) return 1;
    }
    return 0;
}

/* Onaltilik sayi okur; en fazla 8 hane, tasma yok. Donus: 0 basarili, -1 hata. */
static int hb_hex(const uint8_t *in, size_t n, size_t *pos, size_t *out){
    size_t v = 0; int digits = 0;
    while(*pos < n){
        uint8_t c = in[*pos];
        int d;
        if(c >= '0' && c <= '9') d = c - '0';
        else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if(digits >= 8) return -1;
        v = (v << 4) | (size_t)d;
        digits++;
        (*pos)++;
    }
    if(digits == 0) return -1;
    *out = v;
    return 0;
}

long http_dechunk(const uint8_t *in, size_t n, uint8_t *out, size_t cap){
    size_t p = 0, o = 0;
    for(;;){
        size_t sz;
        if(p >= n) return -2;
        if(hb_hex(in, n, &p, &sz) != 0) return -1;
        /* ayrac: ';' ile baslayan chunk-uzantisi olabilir, sonra CRLF */
        while(p < n && in[p] != '\r' && in[p] != '\n') p++;
        if(p + 1 >= n) return -2;
        if(in[p] != '\r' || in[p + 1] != '\n') return -1;
        p += 2;
        if(sz == 0){
            /* trailer'lar: CRLF ile biten satirlar, bos satir (CRLF) ile son */
            for(;;){
                if(p + 1 >= n) return -2;
                if(in[p] == '\r' && in[p + 1] == '\n'){ p += 2; return (long)o; }
                while(p < n && in[p] != '\n') p++;
                if(p >= n) return -2;
                p++;
            }
        }
        if(p + sz + 2 > n) return -2;
        if(o + sz > cap) return -3;
        for(size_t i = 0; i < sz; i++) out[o++] = in[p + i];
        p += sz;
        if(in[p] != '\r' || in[p + 1] != '\n') return -1;
        p += 2;
    }
}
