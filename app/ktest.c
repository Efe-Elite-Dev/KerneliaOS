/* KerneliaOS cekirdek ici testleri (yalnizca WIND_TEST derlemesinde include edilir).
   Sonuclar seri porta yazilir; QEMU isa-debug-exit ile cikis kodu bildirilir:
     0 -> kod 1 (gecti)    1 -> kod 3 (basarisiz)    2 -> kod 5 (panik)
   Test calistiricisi: tools/qemu_test.sh
   Ek bayraklar: WIND_TEST_FAIL (kasitli basarisizlik), WIND_TEST_PANIC (kasitli ud2 paniği). */
#ifdef WIND_TEST

static unsigned KT_PASS = 0, KT_FAIL = 0;

static void ser_dec(unsigned v){
    char b[12]; int k = 0;
    do { b[k++] = (char)('0' + v % 10); v /= 10; } while(v && k < 11);
    while(k) ser_putc(b[--k]);
}

static void kt_check(const char *name, int ok){
    ser_puts(ok ? "[PASS] " : "[FAIL] ");
    ser_puts(name);
    ser_puts("\n");
    if(ok) KT_PASS++; else KT_FAIL++;
}

/* Yaniti elle kurulmus bir DNS A yaniti: "test." icin 10.0.2.2 */
static int kt_dns_fixture(unsigned char *m, unsigned cap, unsigned rcode){
    static const unsigned char tail[] = {
        0x00, 0x01, 0x00, 0x01,              /* soru: A, IN */
        0xC0, 0x0C,                          /* ad: isaretci (ofset 12) */
        0x00, 0x01, 0x00, 0x01,              /* A, IN */
        0x00, 0x00, 0x00, 0x3C,              /* TTL 60 */
        0x00, 0x04, 0x0A, 0x00, 0x02, 0x02   /* RDLEN 4, 10.0.2.2 */
    };
    static const unsigned char head[] = {
        0x12, 0x34, 0x81, 0x00, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 0,
        0x04, 't', 'e', 's', 't', 0x00
    };
    unsigned n = sizeof(head) + sizeof(tail);
    if(cap < n) return 0;
    for(unsigned i = 0; i < sizeof(head); i++) m[i] = head[i];
    for(unsigned i = 0; i < sizeof(tail); i++) m[sizeof(head) + i] = tail[i];
    m[3] = (unsigned char)((m[3] & 0xF0) | (rcode & 0x0F));
    return (int)n;
}

static void kt_run(void){
    /* 1. Zamanlayici: birkac tick gecmeli (kesmeler calisiyor) */
    u32 t0 = TICKS;
    for(u32 spin = 0; spin < 200 && TICKS - t0 < 5; spin++) __asm__ volatile("hlt");
    kt_check("zamanlayici tick ilerliyor", TICKS - t0 >= 5);

    /* 2. Sayfalama ve heap (mem_init sirasinda calisan kendi testleri) */
    kt_check("sayfalama: ayri adres uzayi testi", PG_TEST_OK);
    kt_check("heap: kmalloc/kfree self-test", HEAP_TEST_OK);

    /* 3. Fiziksel cerceve ayiricisi: alloc/free sayaci tutarli kalmali */
    unsigned n0 = FRAME_FREE_N;
    unsigned long long pa = frame_alloc();
    kt_check("frame_alloc: hizali ve sifir disi", pa != 0 && (pa % FRAME_SIZE) == 0);
    kt_check("frame_alloc: bos sayaci bir azalir", FRAME_FREE_N + 1 == n0);
    frame_free(pa);
    kt_check("frame_free: bos sayaci geri gelir", FRAME_FREE_N == n0);

    /* 4. Heap: yaz, oku, serbest birak */
    unsigned char *hp = (unsigned char *)kmalloc(256);
    int hok = (hp != 0);
    if(hok){
        for(unsigned i = 0; i < 256; i++) hp[i] = (unsigned char)(i ^ 0x5A);
        for(unsigned i = 0; i < 256 && hok; i++) if(hp[i] != (unsigned char)(i ^ 0x5A)) hok = 0;
        kfree(hp);
    }
    kt_check("kmalloc(256): yazma/okuma dogru", hok);

    /* 5. DNS yanit ayristirma (saf fonksiyon, ag gerekmez) */
    unsigned char msg[96];
    unsigned ip = 0;
    int ml = kt_dns_fixture(msg, sizeof(msg), 0);
    kt_check("dns: A yaniti ayristirilir", ml > 0 && dns_parse_a(msg, (unsigned)ml, 0x1234, &ip) == DNS_OK);
    kt_check("dns: IP dogru (10.0.2.2)", ip == 0x0A000202u);
    kt_check("dns: kimlik uyusmazligi reddedilir", dns_parse_a(msg, (unsigned)ml, 0x4321, &ip) == DNS_NONE);
    ml = kt_dns_fixture(msg, sizeof(msg), 3);
    kt_check("dns: NXDOMAIN (RCODE 3) taninir", ml > 0 && dns_parse_a(msg, (unsigned)ml, 0x1234, &ip) == DNS_NXDOMAIN);

#ifdef WIND_TEST_FAIL
    kt_check("kasitli basarisizlik (negatif test)", 0);
#endif

    ser_puts("WIND_TEST: gecen="); ser_dec(KT_PASS);
    ser_puts(" kalan="); ser_dec(KT_FAIL); ser_puts("\n");

#ifdef WIND_TEST_PANIC
    ser_puts("WIND_TEST: kasitli panik (ud2)\n");
    __asm__ volatile("ud2");
#endif
    if(KT_FAIL == 0){
        ser_puts("WIND_TEST: PASS\n");
        dbg_exit(0);
    }
    ser_puts("WIND_TEST: FAIL\n");
    dbg_exit(1);
}

#endif
