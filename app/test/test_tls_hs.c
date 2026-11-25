/* TLS 1.3 istemci el sikismasi: gercek sunucuya (openssl s_server) TCP uzerinden baglanir.
   Kullanim: test_tls_hs <port> <ca.der> <host> <ok|fail>
   ok: el sikisma + HTTP istegi basarili olmali. fail: el sikisma reddedilmeli.
   Calistirma: tools/tls_interop.sh (sunucuyu baslatir, tum durumlari dener).
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_tls_hs app/test/test_tls_hs.c */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../crypto/aes.c"
#include "../crypto/gcm.c"
#include "../crypto/sha256.c"
#include "../crypto/hkdf.c"
#include "../crypto/tls13.c"
#include "../crypto/bignum.c"
#include "../crypto/x25519.c"
#include "../crypto/rsa.c"
#include "../crypto/ecdsa.c"
#include "../crypto/x509.c"
#include "../crypto/tls_hs.c"

static int sock_send(void *ctx, const uint8_t *b, size_t n){
    int fd = *(int *)ctx;
    while(n > 0){
        ssize_t w = write(fd, b, n);
        if(w <= 0) return -1;
        b += w; n -= (size_t)w;
    }
    return 0;
}

static int sock_recv(void *ctx, uint8_t *b, size_t n){
    int fd = *(int *)ctx;
    while(n > 0){
        ssize_t r = read(fd, b, n);
        if(r <= 0) return -1;
        b += r; n -= (size_t)r;
    }
    return 0;
}

static tls_hs SESS;                  /* buyuk yapi: yigin yerine statik */
static uint8_t CA[4096];
static uint8_t RESP[4096];

int main(int argc, char **argv){
    if(argc != 5){
        fprintf(stderr, "kullanim: test_tls_hs <port> <ca.der> <host> <ok|fail>\n");
        return 2;
    }
    int port = atoi(argv[1]);
    int want_ok = strcmp(argv[4], "ok") == 0;

    FILE *f = fopen(argv[2], "rb");
    if(!f){ printf("CA dosyasi acilamadi\n"); return 2; }
    size_t calen = fread(CA, 1, sizeof CA, f);
    fclose(f);

    uint8_t ent[64];
    FILE *r = fopen("/dev/urandom", "rb");
    if(!r || fread(ent, 1, 64, r) != 64){ printf("rastgelelik okunamadi\n"); return 2; }
    fclose(r);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(0x7F000001u);
    if(fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0){
        printf("baglanti kurulamadi\n");
        return 1;
    }

    tls_io io = {sock_send, sock_recv, &fd};
    int rc = tls_hs_connect(&SESS, &io, argv[3], strlen(argv[3]), CA, calen, ent);

    if(!want_ok){
        if(rc == 0){ printf("BASARISIZ: beklenmeyen basari (kotu sertifika kabul edildi)\n"); return 1; }
        printf("el sikisma reddedildi (beklenen)\n");
        return 0;
    }
    if(rc != 0){ printf("BASARISIZ: el sikisma basarisiz\n"); return 1; }

    const char *req = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if(tls_hs_write(&SESS, (const uint8_t *)req, strlen(req)) != 0){
        printf("BASARISIZ: istek gonderilemedi\n");
        return 1;
    }
    size_t tot = 0;
    for(;;){
        int n = tls_hs_read(&SESS, RESP + tot, sizeof RESP - tot - 1);
        if(n < 0){ printf("BASARISIZ: okuma hatasi\n"); return 1; }
        if(n == 0) break;
        tot += (size_t)n;
        if(tot >= sizeof RESP - 1) break;
    }
    RESP[tot] = 0;
    if(tot < 5 || memcmp(RESP, "HTTP/", 5) != 0){
        printf("BASARISIZ: HTTP yaniti yok\n");
        return 1;
    }
    size_t line = 0;
    while(line < tot && RESP[line] != '\r' && RESP[line] != '\n') line++;
    printf("TLS 1.3 OK, yanit: %.*s\n", (int)line, (const char *)RESP);
    return 0;
}
