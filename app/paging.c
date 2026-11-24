/* WindOS sayfalama (32-bit, 4 KB sayfalar) ve istisna isleyicileri.
   kernel.c icinde include edilir; once sunlar tanimli olmali: u32/u16/u8 tipleri, idt_gate,
   IDT, hex_str, app_str, dec_str, fr, ds, swap_buffers, SW, SH, PITCH, CW, tl_add.
   mem.c'den once degil, sonra include edilir (frame_alloc, heap_init, kmalloc). */

extern unsigned char _kernel_end[];   /* linker.ld: kernel + bss sonu */

#define PG_P    0x001u
#define PG_RW   0x002u
#define PG_US   0x004u
#define PG_ADDR 0xFFFFF000u

/* Cekirdek heap: 4 MB, kmalloc / kfree bunu kullanir */
#define KHEAP_SIZE (4u * 1024u * 1024u)
static unsigned char KHEAP[KHEAP_SIZE] __attribute__((aligned(4096)));

/* Ilk kez kurulan durumlar (mem_report gosterir) */
static unsigned PG_ON = 0;          /* sayfalama acik */
static unsigned PG_TEST_OK = 0;     /* ayri adres uzayi testi gecti */
static unsigned HEAP_TEST_OK = 0;   /* heap kendi kendini sinamasi gecti */
static unsigned PG_KDIR = 0;        /* cekirdek sayfa dizini (fiziksel = sanal) */
static unsigned long long MEM_RAM_TOP = 0;

/* ------------------------------------------------------------------------ */
/* Istisna isleyicileri: 0..31. Hepsi kirmizi ekran basar ve durur.           */
/* Sayfalama acildiktan sonra bir hata, sessiz yeniden baslatma yerine       */
/* okunabilir bir mesaj verir.                                              */
/* ------------------------------------------------------------------------ */
static void __attribute__((noinline)) exc_panic(u32 n){
    __asm__ volatile("cli");
    u32 cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    const u32 bg = 0xFF5A0000u;
    fr(0, 0, (i32)SW, (i32)SH, bg);
    char hx[12], line[TL_W];
    ds(20, 20, "CEKIRDEK HATASI", CW, bg, 2);
    hex_str(n, 2, hx);
    app_str(line, "VEKTOR: 0x", hx);
    ds(20, 70, line, CW, bg, 2);
    hex_str(cr2, 8, hx);
    app_str(line, "CR2: 0x", hx);
    ds(20, 110, line, CW, bg, 2);
    swap_buffers();
    for(;;) __asm__ volatile("hlt");
}

/* Kullanici modundan (CS RPL=3) gelen istisna: program sonlanir (usermode.c) */
static void user_fault(u32 n, u32 cr2);

/* f: istisna cercevesi (eip, cs, eflags, ...). cs'nin RPL'i 3 ise kullanici hatasi. */
static void __attribute__((noinline)) exc_common(u32 n, void *f){
    u32 cs = ((volatile u32 *)f)[1];
    if(cs & 3u){
        u32 cr2 = 0;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        user_fault(n, cr2);
    }
    exc_panic(n);
}

#define EXC_STUB(n)  __attribute__((interrupt, target("general-regs-only"))) static void exc##n(void *f){ exc_common(n, f); }
#define EXC_STUBE(n) __attribute__((interrupt, target("general-regs-only"))) static void exc##n(void *f, u32 err){ (void)err; exc_common(n, f); }
EXC_STUB(0)   EXC_STUB(1)   EXC_STUB(2)   EXC_STUB(3)   EXC_STUB(4)   EXC_STUB(5)   EXC_STUB(6)   EXC_STUB(7)
EXC_STUBE(8)  EXC_STUB(9)   EXC_STUBE(10) EXC_STUBE(11) EXC_STUBE(12) EXC_STUBE(13) EXC_STUBE(14) EXC_STUB(15)
EXC_STUB(16)  EXC_STUBE(17) EXC_STUB(18) EXC_STUB(19) EXC_STUB(20) EXC_STUB(21) EXC_STUB(22) EXC_STUB(23)
EXC_STUB(24)  EXC_STUB(25) EXC_STUB(26) EXC_STUB(27) EXC_STUB(28) EXC_STUB(29) EXC_STUB(30) EXC_STUB(31)

#define EXC_GATE(n) idt_gate(n, (u32)(unsigned long)exc##n, cs);

/* Istisna kapilarini kurar ve IDT'yi yukler. Sayfalamadan ONCE cagrilmali. */
static void exc_install(void){
    u16 cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    EXC_GATE(0)  EXC_GATE(1)  EXC_GATE(2)  EXC_GATE(3)  EXC_GATE(4)  EXC_GATE(5)  EXC_GATE(6)  EXC_GATE(7)
    EXC_GATE(8)  EXC_GATE(9)  EXC_GATE(10) EXC_GATE(11) EXC_GATE(12) EXC_GATE(13) EXC_GATE(14) EXC_GATE(15)
    EXC_GATE(16) EXC_GATE(17) EXC_GATE(18) EXC_GATE(19) EXC_GATE(20) EXC_GATE(21) EXC_GATE(22) EXC_GATE(23)
    EXC_GATE(24) EXC_GATE(25) EXC_GATE(26) EXC_GATE(27) EXC_GATE(28) EXC_GATE(29) EXC_GATE(30) EXC_GATE(31)
    struct __attribute__((packed)) { u16 limit; u32 base; } p;
    p.limit = (u16)(sizeof(IDT) - 1);
    p.base  = (u32)(unsigned long)IDT;
    __asm__ volatile("lidt %0" :: "m"(p));
}

/* ------------------------------------------------------------------------ */
/* Sayfa tablolari                                                          */
/* ------------------------------------------------------------------------ */

/* Bos bir sayfa dizini / tablosu: bir cerceve ayirir ve sifirlar. Fiziksel adres doner. */
static unsigned pg_new_frame(void){
    unsigned long long pa = frame_alloc();
    if(!pa) return 0;
    volatile u32 *p = (volatile u32 *)(unsigned long)pa;
    for(unsigned i = 0; i < 1024; i++) p[i] = 0;
    return (unsigned)pa;
}

static void pg_cr3(unsigned dir){
    __asm__ volatile("mov %0, %%cr3" :: "r"(dir) : "memory");
}

/* sanal va -> fiziksel pa eslemesini dir uzerinde kurar. Tablo yoksa olusturur. */
static int pg_map(unsigned dir, u32 va, u32 pa, u32 flags){
    volatile u32 *d = (volatile u32 *)(unsigned long)dir;
    u32 di = va >> 22, ti = (va >> 12) & 0x3FFu;
    if(!(d[di] & PG_P)){
        unsigned t = pg_new_frame();
        if(!t) return -1;
        d[di] = t | PG_P | PG_RW | (flags & PG_US);
    }
    volatile u32 *t = (volatile u32 *)(unsigned long)(d[di] & PG_ADDR);
    t[ti] = (pa & PG_ADDR) | PG_P | (flags & (PG_RW | PG_US));
    return 0;
}

/* [start, end) araligini sanal = fiziksel olarak eslestirir */
static int pg_map_identity(unsigned dir, unsigned long long start, unsigned long long end, u32 flags){
    for(unsigned long long a = start & ~0xFFFull; a < end; a += 4096ull){
        if(pg_map(dir, (u32)a, (u32)a, flags) != 0) return -1;
    }
    return 0;
}

/* Yeni adres uzayi: cekirdegin ust dizin girislerini paylasir (cekirdek tablolari ortaktir).
   Kullanici eslemeleri, cekirdegin kullanmadigi dizin girislerine (ornek 0x40000000+) konmali. */
static unsigned pg_space_new(void){
    unsigned dir = pg_new_frame();
    if(!dir) return 0;
    volatile u32 *d = (volatile u32 *)(unsigned long)dir;
    volatile u32 *k = (volatile u32 *)(unsigned long)PG_KDIR;
    for(unsigned i = 0; i < 1024; i++) d[i] = k[i];
    return dir;
}

/* Cekirdek kimlik eslemesini kurar ve sayfalamayi acar.
   RAM: [0, ram_top); framebuffer: [fb_start, fb_end). */
static int pg_enable(unsigned long long ram_top, unsigned long long fb_start, unsigned long long fb_end){
    unsigned dir = pg_new_frame();
    if(!dir) return -1;
    PG_KDIR = dir;
    if(pg_map_identity(dir, 0, ram_top, PG_RW) != 0) return -1;
    if(fb_end > fb_start && pg_map_identity(dir, fb_start, fb_end, PG_RW) != 0) return -1;
    pg_cr3(dir);
    unsigned cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                       /* PG */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    PG_ON = 1;
    return 0;
}

/* Ayri adres uzayi testi: yeni dizinde 0x40000000 eslenir, icine yazilir ve
   fiziksel cerceveden dogrulanir; cekirdek dizininde bu sanal adres bos kalmalidir. */
static int pg_selftest(void){
    unsigned sp = pg_space_new();
    if(!sp) return -1;
    unsigned long long pf = frame_alloc();
    if(!pf) return -1;
    const u32 VA = 0x40000000u;
    if(pg_map(sp, VA, (u32)pf, PG_RW) != 0) return -1;
    volatile u32 *kd = (volatile u32 *)(unsigned long)PG_KDIR;
    if(kd[VA >> 22] & PG_P) return -1;        /* cekirdekte bu dizin girisi olmamali */

    pg_cr3(sp);
    volatile u32 *v = (volatile u32 *)(unsigned long)VA;
    *v = 0xC0FFEEu;
    u32 back = *v;
    pg_cr3(PG_KDIR);

    volatile u32 *phys = (volatile u32 *)(unsigned long)pf;
    frame_free(pf);
    if(back != 0xC0FFEEu || *phys != 0xC0FFEEu) return -1;
    return 0;
}

/* Bellek kurulumu: fiziksel cerceveler (multiboot bellek haritasi), heap, sayfalama.
   Sayfalama kapaliysa kernel yine calisir; yalnizca durumu raporlanir. */
static void mem_init(multiboot_info_t *mbi){
    unsigned long long ram_top = 0;
    frame_init();

    if(mbi->flags & (1u << 6)){                       /* bellek haritasi (mmap) */
        unsigned p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;
        while(p < end){
            unsigned size = *(volatile unsigned *)(unsigned long)p;
            unsigned long long base = *(volatile unsigned long long *)(unsigned long)(p + 4);
            unsigned long long len  = *(volatile unsigned long long *)(unsigned long)(p + 12);
            unsigned type = *(volatile unsigned *)(unsigned long)(p + 20);
            if(type == 1){                            /* kullanilabilir RAM */
                frame_range(base, base + len, 0);
                if(base + len > ram_top) ram_top = base + len;
            }
            p += size + 4;
        }
    } else {                                          /* yedek: mem_upper (KB) */
        ram_top = 0x100000ull + (unsigned long long)mbi->mem_upper * 1024ull;
        frame_range(0x100000ull, ram_top, 0);
    }

    /* Kullanimda kalmasi gerekenler */
    frame_range(0, (unsigned long long)(unsigned long)_kernel_end, 1);   /* kernel + bss + heap */
    frame_range((unsigned long long)(unsigned long)mbi,
                (unsigned long long)(unsigned long)mbi + sizeof(multiboot_info_t), 1);
    if(mbi->flags & (1u << 6))
        frame_range(mbi->mmap_addr, (unsigned long long)mbi->mmap_addr + mbi->mmap_length, 1);
    if(mbi->flags & (1u << 3)){                       /* GRUB modulleri (disk imaji) */
        for(unsigned i = 0; i < mbi->mods_count; i++){
            volatile unsigned *m = (volatile unsigned *)(unsigned long)(mbi->mods_addr + 16u * i);
            frame_range(m[0], m[1], 1);
        }
    }

    if(ram_top < 0x100000ull) ram_top = 0x100000ull;
    if(ram_top > FRAME_LIMIT) ram_top = FRAME_LIMIT;
    MEM_RAM_TOP = ram_top;

    if(heap_init(KHEAP, KHEAP_SIZE) == 0) HEAP_TEST_OK = (heap_selftest() == 0);

    unsigned long long fb_s = mbi->framebuffer_addr;
    unsigned long long fb_e = fb_s + (unsigned long long)PITCH * SH;
    fb_s &= ~0xFFFull;
    fb_e = (fb_e + 0xFFFull) & ~0xFFFull;
    if(pg_enable(ram_top, fb_s, fb_e) == 0){
        PG_TEST_OK = (pg_selftest() == 0);
    }
}

/* Terminal icin bellek raporu (terminal hazir olunca cagrilir) */
static void mem_report(void){
    char a[TL_W], n[16];
    dec_str((u32)(MEM_RAM_TOP / (1024ull * 1024ull)), n);
    app_str(a, "BELLEK: RAM ", n);
    tl_add(a);
    dec_str(FRAME_FREE_N / 256u, n);                  /* 4 KB cerceve -> MB */
    app_str(a, "  BOS (MB): ", n);
    tl_add(a);
    tl_add(PG_ON ? (PG_TEST_OK ? "SAYFALAMA: ACIK, ayri adres testi GECTI"
                               : "SAYFALAMA: ACIK, ayri adres testi BASARISIZ")
                 : "SAYFALAMA: KAPALI");
    tl_add(HEAP_TEST_OK ? "HEAP: kmalloc/kfree testi GECTI" : "HEAP: TEST BASARISIZ");
}
