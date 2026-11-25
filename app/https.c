/* KerneliaOS HTTPS istemcisi: TLS 1.3 el sikismasi (crypto/tls_hs.c) kernel TCP yigini uzerinden.
   kernel.c icinden include edilir; TCP, DNS ve URL fonksiyonlarindan sonra gelir.
   Guven deposu: yalnizca WIND_TLS_TESTCA ile uretilen test CA'si. Uretim surumunde bos:
   bu durumda https komutu sertifika dogrulayamadigi icin baglanmayi reddeder. */

#include "crypto/sha256.c"
#include "crypto/hkdf.c"
#include "crypto/aes.c"
#include "crypto/gcm.c"
#include "crypto/tls13.c"
#include "crypto/bignum.c"
#include "crypto/x25519.c"
#include "crypto/rsa.c"
#include "crypto/ecdsa.c"
#include "crypto/x509.c"
#include "crypto/rng.c"
#include "crypto/http_body.c"
#include "crypto/tls_hs.c"

#ifdef WIND_TLS_TESTCA
#include "tls_testca.h"              /* HTTPS_TESTCA_DER, HTTPS_TESTCA_LEN (tools/qemu_test.sh) */
#define HTTPS_TRUST_DER HTTPS_TESTCA_DER
#define HTTPS_TRUST_LEN HTTPS_TESTCA_LEN
#else
#define HTTPS_TRUST_DER ((const uint8_t *)0)
#define HTTPS_TRUST_LEN 0
#endif

#define HTTPS_OUT_CAP 131072

static tls_hs HTTPS_S;               /* buyuk yapi: statik (kernel yiginina sigmaz) */
static int HTTPS_RD = 0;             /* TCP_RX icinde TLS'in okudugu konum */
static u8 HTTPS_OUT[HTTPS_OUT_CAP];  /* cozulmus yanit (HTTP basliklari + govde) */

/* TLS io: yazma TCP'ye, okuma TCP_RX akisindan (tam n bayt; zaman asimi -> hata) */
static int https_send(void *ctx, const uint8_t *b, size_t n){
    (void)ctx;
    return tcp_send_reliable((const u8 *)b, (int)n) ? 0 : -1;
}

static int https_recv(void *ctx, uint8_t *b, size_t n){
    (void)ctx;
    u32 until = TICKS + 300;
    while(TCP_RXN - HTTPS_RD < (int)n){
        if(TCP_FIN || TCP_STATE != TS_ESTAB) return -1;
        if(TICKS >= until) return -1;
        nic_poll();
    }
    for(size_t i = 0; i < n; i++) b[i] = TCP_RX[HTTPS_RD + (int)i];
    HTTPS_RD += (int)n;
    return 0;
}

/* Baglan, el sikis, GET gonder, yaniti HTTPS_OUT'a coz.
   Donus: 0 basarili (*outn bayt); -1 TCP; -2 RDRAND yok; -3 el sikisma reddi; -4 yanit yok */
static int https_fetch(u32 ip, u16 port, const char *host, size_t hl, const char *path, long *outn){
    if(!tcp_connect(ip, port)) return -1;
    HTTPS_RD = 0;
    uint8_t ent[64];
    if(rng_fill(ent, sizeof ent) != 0){ TCP_STATE = TS_DONE; return -2; }
    tls_io io = {https_send, https_recv, 0};
    if(tls_hs_connect(&HTTPS_S, &io, host, hl, HTTPS_TRUST_DER, HTTPS_TRUST_LEN, ent) != 0){
        TCP_STATE = TS_DONE;
        return -3;
    }
    char req[400];
    int k = 0;
    const char *parts[5] = {
        "GET ", path, " HTTP/1.1\r\nHost: ", host,
        "\r\nUser-Agent: KerneliaOS\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"
    };
    for(int i = 0; i < 5; i++){
        for(int j = 0; parts[i][j] && k < (int)sizeof req - 1; j++) req[k++] = parts[i][j];
    }
    if(tls_hs_write(&HTTPS_S, (const uint8_t *)req, (size_t)k) != 0){ TCP_STATE = TS_DONE; return -4; }
    size_t total = 0;
    while(total < HTTPS_OUT_CAP){
        int n = tls_hs_read(&HTTPS_S, HTTPS_OUT + total, HTTPS_OUT_CAP - total);
        if(n <= 0) break;
        total += (size_t)n;
    }
    TCP_STATE = TS_DONE;
    if(total == 0) return -4;
    *outn = (long)total;
    return 0;
}

/* HTTPS_OUT'taki yaniti (chunked ise cozulmus) TCP_RX'e koyar ve http_show ile gosterir */
static void https_show(long total){
    long bo = http_body_offset(HTTPS_OUT, (size_t)total);
    if(bo < 0) bo = total;
    if(http_is_chunked(HTTPS_OUT, (size_t)total)){
        for(long i = 0; i < bo; i++) TCP_RX[i] = HTTPS_OUT[i];
        long dn = http_dechunk(HTTPS_OUT + bo, (size_t)(total - bo), TCP_RX + bo, (size_t)(TCP_RX_CAP - bo));
        if(dn < 0){
            tl_add("HTTPS: CHUNKED GOVDE EKSIK/BOZUK (basliklar gosteriliyor).");
            TCP_RXN = (int)bo;
        } else {
            TCP_RXN = (int)(bo + dn);
        }
    } else {
        for(long i = 0; i < total; i++) TCP_RX[i] = HTTPS_OUT[i];
        TCP_RXN = (int)total;
    }
    http_show();
}

/* https host[:port][/yol]  (varsayilan port 443). ornek: https 10.0.2.2:4443/ */
static void https_cmd(const char *arg){
    if(!NIC_UP){ tl_add("AG KARTI YOK: QEMU -nic user,model=rtl8139 ile calistirin."); return; }
    if(HTTPS_TRUST_LEN == 0){ tl_add("HTTPS: GUVEN DEPOSU YOK (bu surumde kok sertifika tanimli degil)."); return; }
    const char *q = arg;
    if(q[0] == 'h' && q[1] == 't' && q[2] == 't' && q[3] == 'p' && q[4] == 's' && q[5] == ':' && q[6] == '/' && q[7] == '/')
        q += 8;
    int has_port = 0;
    for(const char *c = q; *c && *c != '/' && *c != ' '; c++) if(*c == ':') has_port = 1;
    u32 ip = 0; u16 port = 443; char host[64], path[96];
    if(!url_parse(q, host, &port, path)){ tl_add("KULLANIM: https host[:port][/yol]  (ornek: https example.com/)"); return; }
    if(!has_port) port = 443;
    if(!parse_ip(host, &ip)){
        int r = dns_resolve(host, DNS_DEFAULT, &ip);
        if(r != DNS_OK){ tl_add(dns_err_text(r)); return; }
    }
    long n = 0;
    int r = https_fetch(ip, port, host, (size_t)klen(host), path, &n);
    if(r == -1){ tl_add("BAGLANTI KURULAMADI (TCP ZAMAN ASIMI)."); return; }
    if(r == -2){ tl_add("HTTPS: RDRAND YOK (QEMU -cpu max ile calistirin)."); return; }
    if(r == -3){ tl_add("HTTPS: EL SIKISMA REDDEDILDI (sertifika, host ya da protokol)."); return; }
    if(r == -4){ tl_add("HTTPS: YANIT ALINAMADI."); return; }
    https_show(n);
}
