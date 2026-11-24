/* KerneliaOS DNS (RFC 1035) yardimcilari: sorgu olusturma ve yanit ayristirma.
   Saf fonksiyonlar (donanim yok); kernel.c ve host testi (app/test/test_dns.c) kullanir.
   Kapsam: tek soru, A kaydi (IPv4), isaretciler (sikistirma) desteklenir. */
#ifndef WIND_DNS_C
#define WIND_DNS_C

#define DNS_PORT     53
#define DNS_NAME_MAX 255

/* Donus kodlari: dns_parse_a icin */
#define DNS_OK        1     /* A kaydi bulundu */
#define DNS_NONE      0     /* yanit gecerli ama A kaydi yok, ya da yanit bozuk */
#define DNS_NXDOMAIN (-2)   /* sunucu: boyle bir ad yok (RCODE 3) */

/* "ornek.com" -> <len>ornek<len>com<0>. Donus: yazilan bayt sayisi, hata -1. */
static int dns_encode_name(unsigned char *o, unsigned cap, const char *name){
    unsigned k = 0;
    const char *p = name;
    if(!*p) return -1;
    while(*p){
        unsigned len = 0;
        while(p[len] && p[len] != '.') len++;
        if(len == 0 || len > 63) return -1;
        if(k + 1 + len >= cap) return -1;
        o[k++] = (unsigned char)len;
        for(unsigned i = 0; i < len; i++) o[k++] = (unsigned char)p[i];
        p += len;
        if(*p == '.') p++;
    }
    if(k + 1 > cap || k + 1 > DNS_NAME_MAX + 1) return -1;
    o[k++] = 0;
    return (int)k;
}

/* Sorgu: basllik (RD=1, 1 soru) + A/IN sorusu. Donus: toplam uzunluk, hata -1. */
static int dns_build_query(unsigned char *b, unsigned cap, unsigned short id, const char *name){
    if(cap < 12) return -1;
    b[0] = (unsigned char)(id >> 8);
    b[1] = (unsigned char)id;
    b[2] = 0x01;                 /* RD */
    b[3] = 0x00;
    b[4] = 0x00; b[5] = 0x01;    /* QDCOUNT = 1 */
    for(unsigned i = 6; i < 12; i++) b[i] = 0;
    int q = dns_encode_name(b + 12, cap - 12, name);
    if(q < 0) return -1;
    unsigned k = 12 + (unsigned)q;
    if(k + 4 > cap) return -1;
    b[k] = 0x00; b[k + 1] = 0x01;      /* QTYPE = A */
    b[k + 2] = 0x00; b[k + 3] = 0x01;  /* QCLASS = IN */
    return (int)(k + 4);
}

/* Bir ad alaninin sonrasini dondurur (isaretci atlanir). Sonsuz dongu ve sinir disi -> -1. */
static int dns_skip_name(const unsigned char *m, unsigned n, unsigned off){
    unsigned pos = off, jumps = 0;
    int end = -1;
    for(;;){
        if(pos >= n) return -1;
        unsigned char l = m[pos];
        if(l == 0){
            if(end < 0) end = (int)pos + 1;
            return end;
        }
        if((l & 0xC0) == 0xC0){                       /* isaretci: 2 bayt */
            if(pos + 1 >= n) return -1;
            if(end < 0) end = (int)pos + 2;
            if(++jumps > 16) return -1;
            pos = (((unsigned)l & 0x3F) << 8) | m[pos + 1];
            continue;
        }
        if(l & 0xC0) return -1;                       /* ayrilmis tipler desteklenmez */
        pos += 1u + l;
        if(pos > n) return -1;
    }
}

/* Yaniti ayristirir: basarili bir A kaydi varsa *ip_out'a yazar ve DNS_OK doner.
   Kimlik (id) eslesmeli, QR=1 olmali. RCODE 3 ise DNS_NXDOMAIN. */
static int dns_parse_a(const unsigned char *m, unsigned n, unsigned short id, unsigned *ip_out){
    if(n < 12) return DNS_NONE;
    if((((unsigned)m[0] << 8) | m[1]) != id) return DNS_NONE;
    if(!(m[2] & 0x80)) return DNS_NONE;
    unsigned rcode = m[3] & 0x0F;
    if(rcode == 3) return DNS_NXDOMAIN;
    if(rcode != 0) return DNS_NONE;
    unsigned qd = ((unsigned)m[4] << 8) | m[5];
    unsigned an = ((unsigned)m[6] << 8) | m[7];
    unsigned off = 12;
    for(unsigned i = 0; i < qd; i++){
        int e = dns_skip_name(m, n, off);
        if(e < 0 || (unsigned)e + 4 > n) return DNS_NONE;
        off = (unsigned)e + 4;
    }
    for(unsigned i = 0; i < an; i++){
        int e = dns_skip_name(m, n, off);
        if(e < 0 || (unsigned)e + 10 > n) return DNS_NONE;
        unsigned type = ((unsigned)m[e] << 8) | m[e + 1];
        unsigned cls  = ((unsigned)m[e + 2] << 8) | m[e + 3];
        unsigned rdl  = ((unsigned)m[e + 8] << 8) | m[e + 9];
        unsigned rd = (unsigned)e + 10;
        if(rd + rdl > n) return DNS_NONE;
        if(type == 1 && cls == 1 && rdl == 4){
            *ip_out = ((unsigned)m[rd] << 24) | ((unsigned)m[rd + 1] << 16) |
                      ((unsigned)m[rd + 2] << 8) | m[rd + 3];
            return DNS_OK;
        }
        off = rd + rdl;
    }
    return DNS_NONE;
}

#endif
