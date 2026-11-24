/* dns.c (DNS sorgu/yanit yardimcilari) icin host testleri.
   Derleme ve calistirma:
     gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_dns app/test/test_dns.c && ./test_dns */
#include <stdio.h>
#include <string.h>
#include "../dns.c"

static int fails = 0, oks = 0;
#define CHECK(name, cond) do { if(cond){ oks++; printf("ok   %s\n", name); } \
                               else { fails++; printf("FAIL %s\n", name); } } while(0)

/* Yanit olusturur: id, rcode, bir soru (ornek.com) ve istege bagli cname + A kaydi */
static int make_response(unsigned char *m, unsigned short id, unsigned rcode, int with_a, unsigned ip){
    int k = 0;
    m[k++] = id >> 8; m[k++] = id & 0xFF;
    m[k++] = 0x81; m[k++] = (unsigned char)(0x80 | rcode);   /* QR=1, RD=1, RA=1 */
    m[k++] = 0; m[k++] = 1;                                  /* QDCOUNT */
    m[k++] = 0; m[k++] = with_a ? 1 : 0;                     /* ANCOUNT */
    m[k++] = 0; m[k++] = 0; m[k++] = 0; m[k++] = 0;
    /* soru: 7example3com0 A IN (offset 12) */
    const unsigned char qn[] = { 7,'e','x','a','m','p','l','e', 3,'c','o','m', 0 };
    memcpy(m + k, qn, sizeof qn); k += sizeof qn;
    m[k++] = 0; m[k++] = 1; m[k++] = 0; m[k++] = 1;
    if(with_a){
        m[k++] = 0xC0; m[k++] = 0x0C;                        /* isaretci: offset 12 */
        m[k++] = 0; m[k++] = 1; m[k++] = 0; m[k++] = 1;      /* A, IN */
        m[k++] = 0; m[k++] = 0; m[k++] = 0x0E; m[k++] = 0x10;/* TTL */
        m[k++] = 0; m[k++] = 4;                              /* RDLENGTH */
        m[k++] = (unsigned char)(ip >> 24); m[k++] = (unsigned char)(ip >> 16);
        m[k++] = (unsigned char)(ip >> 8);  m[k++] = (unsigned char)ip;
    }
    return k;
}

int main(void){
    unsigned char b[300];
    int n;

    /* kodlama */
    n = dns_encode_name(b, sizeof b, "example.com");
    CHECK("ad kodlama uzunlugu", n == 13);
    CHECK("ad kodlama bicimi", memcmp(b, "\x07" "example\x03" "com\x00", 13) == 0);
    CHECK("sondaki nokta kabul", dns_encode_name(b, sizeof b, "a.b.") == 5);
    CHECK("bos ad reddedilir", dns_encode_name(b, sizeof b, "") < 0);
    CHECK("bos etiket reddedilir", dns_encode_name(b, sizeof b, "a..b") < 0);
    char longlbl[80]; memset(longlbl, 'x', 70); longlbl[70] = 0;
    CHECK("64+ karakter etiket reddedilir", dns_encode_name(b, sizeof b, longlbl) < 0);
    CHECK("kucuk tampon reddedilir", dns_encode_name(b, 5, "example.com") < 0);

    /* sorgu */
    n = dns_build_query(b, sizeof b, 0x1234, "example.com");
    CHECK("sorgu uzunlugu", n == 12 + 13 + 4);
    CHECK("sorgu kimligi", b[0] == 0x12 && b[1] == 0x34);
    CHECK("sorgu RD ve QDCOUNT", b[2] == 0x01 && b[4] == 0 && b[5] == 1);
    CHECK("sorgu sonu A/IN", b[n-4] == 0 && b[n-3] == 1 && b[n-2] == 0 && b[n-1] == 1);
    CHECK("sorgu kucuk tampon", dns_build_query(b, 20, 1, "example.com") < 0);

    /* yanit ayristirma */
    unsigned char r[300];
    unsigned ip = 0;
    n = make_response(r, 0x1234, 0, 1, 0x5DB8D822u);             /* 93.184.216.34 */
    CHECK("gecerli A yaniti", dns_parse_a(r, n, 0x1234, &ip) == DNS_OK);
    CHECK("A kaydi IP'si", ip == 0x5DB8D822u);
    CHECK("yanlis kimlik reddedilir", dns_parse_a(r, n, 0x9999, &ip) == DNS_NONE);

    n = make_response(r, 0x1234, 0, 0, 0);
    CHECK("A kaydi yoksa NONE", dns_parse_a(r, n, 0x1234, &ip) == DNS_NONE);

    n = make_response(r, 0x1234, 3, 0, 0);
    CHECK("NXDOMAIN", dns_parse_a(r, n, 0x1234, &ip) == DNS_NXDOMAIN);

    n = make_response(r, 0x1234, 2, 0, 0);                        /* SERVFAIL */
    CHECK("SERVFAIL NONE", dns_parse_a(r, n, 0x1234, &ip) == DNS_NONE);

    /* kesik paketler: hicbir uzunlukta tasma ya da yanlis basari olmamali */
    n = make_response(r, 0x1234, 0, 1, 0x01020304u);
    int bad = 0;
    for(int len = 0; len < n; len++)
        if(dns_parse_a(r, (unsigned)len, 0x1234, &ip) == DNS_OK) bad = 1;
    CHECK("kesik paketler basarisiz", !bad);

    /* dongu olusturan isaretci: sonsuz donmemeli */
    unsigned char loop[64];
    memset(loop, 0, sizeof loop);
    loop[0] = 0x12; loop[1] = 0x34; loop[2] = 0x81; loop[3] = 0x80;
    loop[5] = 1; loop[7] = 0;
    loop[12] = 0xC0; loop[13] = 0x0C;                             /* kendine isaret */
    loop[14] = 0; loop[15] = 1;
    CHECK("isaretci dongusu reddedilir", dns_parse_a(loop, 16, 0x1234, &ip) == DNS_NONE);

    /* A kaydi oncesinde baska kayit (ornek: CNAME) atlanmali */
    unsigned char c[128];
    int k = 0;
    c[k++] = 0x12; c[k++] = 0x34; c[k++] = 0x81; c[k++] = 0x80;
    c[k++] = 0; c[k++] = 1; c[k++] = 0; c[k++] = 2; c[k++] = 0; c[k++] = 0; c[k++] = 0; c[k++] = 0;
    const unsigned char qn[] = { 7,'e','x','a','m','p','l','e', 3,'c','o','m', 0 };
    memcpy(c + k, qn, sizeof qn); k += sizeof qn; c[k++] = 0; c[k++] = 1; c[k++] = 0; c[k++] = 1;
    c[k++] = 0xC0; c[k++] = 0x0C; c[k++] = 0; c[k++] = 5; c[k++] = 0; c[k++] = 1;   /* CNAME */
    c[k++] = 0; c[k++] = 0; c[k++] = 0; c[k++] = 60; c[k++] = 0; c[k++] = 2; c[k++] = 0xC0; c[k++] = 0x0C;
    c[k++] = 0xC0; c[k++] = 0x0C; c[k++] = 0; c[k++] = 1; c[k++] = 0; c[k++] = 1;     /* A */
    c[k++] = 0; c[k++] = 0; c[k++] = 0; c[k++] = 60; c[k++] = 0; c[k++] = 4;
    c[k++] = 10; c[k++] = 0; c[k++] = 2; c[k++] = 2;
    ip = 0;
    CHECK("CNAME atlanip A bulunur", dns_parse_a(c, (unsigned)k, 0x1234, &ip) == DNS_OK && ip == 0x0A000202u);

    printf("\n%d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
