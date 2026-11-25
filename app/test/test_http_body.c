/* HTTP/1.1 yardimcilari: durum kodu, govde konumu, chunked cozme, RNG.
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_http_body app/test/test_http_body.c && ./test_http_body */
#include <stdio.h>
#include <string.h>
#include "../crypto/http_body.c"
#include "../crypto/rng.c"

static int fails = 0, oks = 0;

static void check(const char *name, int ok){
    if(ok){ oks++; }
    else { fails++; printf("BASARISIZ: %s\n", name); }
}

static long dechunk_str(const char *s, uint8_t *out, size_t cap){
    return http_dechunk((const uint8_t *)s, strlen(s), out, cap);
}

int main(void){
    uint8_t out[256];

    /* Durum kodu */
    check("status 200", http_status((const uint8_t *)"HTTP/1.1 200 OK\r\n", 17) == 200);
    check("status 404", http_status((const uint8_t *)"HTTP/1.0 404 Not Found\r\n", 24) == 404);
    check("status bozuk", http_status((const uint8_t *)"HTTP/1.1 2x0 OK\r\n", 16) == -1);
    check("status HTTP degil", http_status((const uint8_t *)"FTP/1.1 200 OK\r\n", 16) == -1);

    /* Govde konumu */
    const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nmerhaba";
    long bo = http_body_offset((const uint8_t *)resp, strlen(resp));
    check("govde konumu", bo == 45 && memcmp(resp + bo, "merhaba", 7) == 0);
    check("baslik sonu yok", http_body_offset((const uint8_t *)"HTTP/1.1 200 OK\r\n", 17) == -1);

    /* chunked algilama (buyuk-kucuk harf) */
    const char *ch = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    const char *ch2 = "HTTP/1.1 200 OK\r\ntransfer-encoding:  CHUNKED\r\n\r\n";
    const char *nch = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    check("chunked algilanir", http_is_chunked((const uint8_t *)ch, strlen(ch)) == 1);
    check("chunked buyuk harf", http_is_chunked((const uint8_t *)ch2, strlen(ch2)) == 1);
    check("chunked degil", http_is_chunked((const uint8_t *)nch, strlen(nch)) == 0);

    /* Chunked cozme: RFC 7230 ornegi */
    long n = dechunk_str("4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", out, sizeof out);
    check("chunked ornek", n == 9 && memcmp(out, "Wikipedia", 9) == 0);

    /* onaltilik buyuk harf ve chunk uzantisi */
    n = dechunk_str("A;name=val\r\n0123456789\r\n0\r\n\r\n", out, sizeof out);
    check("chunk uzantisi ve buyuk hex", n == 10 && memcmp(out, "0123456789", 10) == 0);

    /* Bos govde (yalniz sonlandirici) */
    n = dechunk_str("0\r\n\r\n", out, sizeof out);
    check("bos chunked", n == 0);

    /* Trailer'li */
    n = dechunk_str("2\r\nok\r\n0\r\nX-Trailer: 1\r\n\r\n", out, sizeof out);
    check("trailer ile", n == 2 && memcmp(out, "ok", 2) == 0);

    /* Bozuk: hex olmayan boyut */
    check("bozuk hex reddedilir", dechunk_str("zz\r\nab\r\n0\r\n\r\n", out, sizeof out) == -1);
    /* Bozuk: veri sonunda CRLF yok */
    check("veri sonu CRLF yok", dechunk_str("2\r\nabXX0\r\n\r\n", out, sizeof out) == -1);
    /* Eksik: yanit kesilmis */
    check("kesilmis yanit -2", dechunk_str("4\r\nWi", out, sizeof out) == -2);
    check("sonlandirici yok -2", dechunk_str("2\r\nok\r\n", out, sizeof out) == -2);
    /* Cikti tamponu yetmiyor */
    check("tampon yetmiyor -3", dechunk_str("4\r\nWiki\r\n0\r\n\r\n", out, 2) == -3);
    /* Asiri uzun boyut (8 haneden fazla) */
    check("asiri uzun boyut reddedilir", dechunk_str("123456789\r\nx\r\n0\r\n\r\n", out, sizeof out) == -1);

    /* RNG: RDRAND varsa rastgele ve birbirinden farkli */
    if(rng_available()){
        uint8_t a[32], b[32];
        int ra = rng_fill(a, sizeof a), rb = rng_fill(b, sizeof b);
        int zero = 1, same = 1;
        for(int i = 0; i < 32; i++){
            if(a[i]) zero = 0;
            if(a[i] != b[i]) same = 0;
        }
        check("rng doldurur", ra == 0 && rb == 0);
        check("rng sifir degil", !zero);
        check("rng iki cagri ayni degil", !same);
    } else {
        printf("not: bu CPU'da RDRAND yok, rng testleri atlandi\n");
    }

    printf("http/rng: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
