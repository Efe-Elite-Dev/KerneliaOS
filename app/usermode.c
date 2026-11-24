/* KerneliaOS kullanici modu (ring 3): GDT + TSS, int 0x80 sistem cagrilari, .exe programinin
   ayri bir adres uzayinda (UT_* adresleri) calistirilmasi, kullanici hatalarinda programin
   sonlandirilmasi (cekirdek cokmez).
   kernel.c icinde include edilir. Once sunlar tanimli olmali: TICKS, idt_gate, IDT, PG_* ve
   pg_* (paging.c), frame_alloc/frame_free, kmalloc (heap.c), pe_load/pe_info (pe.h),
   EXE_ESP/EXE_RET/EXE_CODE, exe_putc, exe_flush, exe_name_is, TL_W, tl_add, app_str, hex_str, itoa.
   .wind programlari bu modelin disinda, hala cekirdek modunda calisir. */
#include "userabi.h"

/* ------------------------------------------------------------------------ */
/* GDT ve TSS                                                               */
/*   0x08 kernel kodu   0x10 kernel verisi                                   */
/*   0x1B kullanici kodu (0x18 | RPL3)   0x23 kullanici verisi (0x20 | RPL3) */
/*   0x28 TSS (ring 3'ten ring 0'a gecislerde esp0/ss0 buradan okunur)       */
/* ------------------------------------------------------------------------ */
struct __attribute__((packed)) GdtEntry { u16 lim_lo; u16 base_lo; u8 base_mid; u8 access; u8 gran; u8 base_hi; };

static struct GdtEntry GDT[6] __attribute__((aligned(8)));
static u32 TSS[26] __attribute__((aligned(16)));       /* 104 bayt standart TSS */
static u8  TSS_STACK[16384] __attribute__((aligned(16)));

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran){
    GDT[i].lim_lo  = (u16)(limit & 0xFFFFu);
    GDT[i].base_lo = (u16)(base & 0xFFFFu);
    GDT[i].base_mid = (u8)((base >> 16) & 0xFFu);
    GDT[i].access  = access;
    GDT[i].gran    = (u8)(((limit >> 16) & 0x0Fu) | (gran & 0xF0u));
    GDT[i].base_hi = (u8)((base >> 24) & 0xFFu);
}

static void gdt_init(void){
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFu, 0x9A, 0xCF);     /* kernel kodu, DPL 0 */
    gdt_set(2, 0, 0xFFFFFu, 0x92, 0xCF);     /* kernel verisi, DPL 0 */
    gdt_set(3, 0, 0xFFFFFu, 0xFA, 0xCF);     /* kullanici kodu, DPL 3 */
    gdt_set(4, 0, 0xFFFFFu, 0xF2, 0xCF);     /* kullanici verisi, DPL 3 */
    gdt_set(5, (u32)(unsigned long)TSS, sizeof TSS - 1, 0x89, 0x00);   /* TSS */

    for(unsigned i = 0; i < sizeof TSS / sizeof TSS[0]; i++) TSS[i] = 0;
    TSS[1] = (u32)(unsigned long)TSS_STACK + sizeof TSS_STACK;         /* esp0 */
    TSS[2] = 0x10;                                                     /* ss0 */
    TSS[25] = (u32)sizeof TSS << 16;                                   /* iomap: bitmap yok, ring 3 G/C portlarina erisemez */

    struct __attribute__((packed)) { u16 limit; u32 base; } p;
    p.limit = (u16)(sizeof GDT - 1);
    p.base  = (u32)(unsigned long)GDT;
    __asm__ volatile("lgdt %0" :: "m"(p));
    /* CS'i yeniden yukle (uzak donus), sonra veri segmentlerini */
    __asm__ volatile(
        "pushl $0x08\n\t"
        "pushl $1f\n\t"
        "lret\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        ::: "eax", "memory");
    __asm__ volatile("ltr %0" :: "r"((u16)0x28));
}

/* ------------------------------------------------------------------------ */
/* int 0x80: sistem cagrisi girisi                                          */
/* ------------------------------------------------------------------------ */
u32 sys_dispatch(u32 *fr);

/* pusha yigini: edi(+0) esi(+4) ebp(+8) esp(+12) ebx(+16) edx(+20) ecx(+24) eax(+28) */
__asm__(
    ".text\n"
    ".globl syscall_stub\n"
    "syscall_stub:\n"
    "    pusha\n"
    "    pushl %esp\n"
    "    call sys_dispatch\n"
    "    addl $4, %esp\n"
    "    movl %eax, 28(%esp)\n"
    "    popa\n"
    "    iret\n"
);

/* Kullanici adres araligi ve sayfa izni kontrolu (mevcut CR3 uzerinden) */
static int user_page_ok(u32 va){
    u32 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    volatile u32 *d = (volatile u32 *)(unsigned long)cr3;
    u32 pde = d[va >> 22];
    if(!(pde & PG_P) || !(pde & PG_US)) return 0;
    volatile u32 *t = (volatile u32 *)(unsigned long)(pde & PG_ADDR);
    u32 pte = t[(va >> 12) & 0x3FFu];
    return (pte & PG_P) && (pte & PG_US);
}

static int user_range_ok(u32 va, u32 n){
    if(n == 0) return 1;
    u32 end = va + n - 1;
    if(end < va || va < UT_LO || end >= UT_HI) return 0;
    for(u32 p = va & ~0xFFFu; ; p += 4096u){
        if(!user_page_ok(p)) return 0;
        if(p >= (end & ~0xFFFu)) break;
    }
    return 1;
}

static void __attribute__((noreturn)) user_finish(u32 code);

u32 sys_dispatch(u32 *fr){
    u32 num = fr[7], a1 = fr[4], a2 = fr[6], a3 = fr[5], a4 = fr[1];
    switch(num){
    case SYS_EXIT:
        user_finish(a1);
    case SYS_GETSTDHANDLE:
        return 0x100u + (a1 & 0xFFu);                 /* sahte tutamac */
    case SYS_WRITE: {
        if(a3 > 65536u || !user_range_ok(a2, a3)) return 0;
        const u8 *b = (const u8 *)(unsigned long)a2;
        for(u32 i = 0; i < a3; i++) exe_putc((char)b[i]);
        if(a4 && user_range_ok(a4, 4)) *(volatile u32 *)(unsigned long)a4 = a3;
        return 1;
    }
    case SYS_TICKS:
        return (u32)TICKS;
    case SYS_LASTERR:
        return 0;                                    /* hata kodu takibi henuz yok */
    default:
        return 0xFFFFFFFFu;
    }
}

void syscall_stub(void);          /* asm giris (yukarida tanimli) */

static void user_init(void){
    idt_gate(0x80, (u32)(unsigned long)syscall_stub, 0x08);
    IDT[0x80].type = 0xEE;                            /* DPL 3: ring 3 'int 0x80' cagirabilir */
}

/* ------------------------------------------------------------------------ */
/* Kullanici adres uzayi                                                    */
/* ------------------------------------------------------------------------ */
static unsigned USER_DIR = 0;         /* calisan programin sayfa dizini (0: yok) */
static u32 USER_ENTRY = 0, USER_SP = 0;

/* Sayfa ayir, sifirla (ya da src'den doldur), sanal adrese esle. Fiziksel adres doner, hata 0. */
static unsigned user_page(unsigned dir, u32 va, u32 flags, const u8 *src){
    unsigned long long pa = frame_alloc();
    if(!pa) return 0;
    volatile u8 *d = (volatile u8 *)(unsigned long)pa;
    for(u32 i = 0; i < 4096u; i++) d[i] = src ? src[i] : 0;
    if(pg_map(dir, va, (u32)pa, flags) != 0){ frame_free(pa); return 0; }
    return (unsigned)pa;
}

/* Programin sayfa dizini ve tablolarini (kernel ile paylasilmayan) serbest birakir */
static void user_free(unsigned dir){
    volatile u32 *ud = (volatile u32 *)(unsigned long)dir;
    volatile u32 *kd = (volatile u32 *)(unsigned long)PG_KDIR;
    for(u32 di = 0; di < 1024u; di++){
        u32 pde = ud[di];
        if(!(pde & PG_P) || pde == kd[di]) continue;          /* kernel tablosu: dokunma */
        volatile u32 *t = (volatile u32 *)(unsigned long)(pde & PG_ADDR);
        for(u32 ti = 0; ti < 1024u; ti++){
            u32 pte = t[ti];
            if(pte & PG_P) frame_free(pte & PG_ADDR);
        }
        frame_free(pde & PG_ADDR);
        ud[di] = 0;
    }
    frame_free(dir);
}

/* ------------------------------------------------------------------------ */
/* kernel32 import stub'lari                                                */
/*   mov eax, N ; mov ebx/ecx/edx/esi/edi, [esp+4..] ; int 0x80 ; ret k*4   */
/* Stub'lar UT_STUB sayfasinda, her biri 32 bayt. popa tum yazmaclari geri  */
/* yukledigi icin stdcall'in korunmasi gereken yazmaclari bozulmaz.         */
/* ------------------------------------------------------------------------ */
#define STUB_SLOT 32u
#define STUB_MAX  (4096u / STUB_SLOT)
static u8  STUB_BUF[4096] __attribute__((aligned(16)));
static u32 STUB_N = 0;

static const struct { const char *name; u32 num; u32 argc; } SYSMAP[] = {
    { "ExitProcess",    SYS_EXIT,         1 },
    { "GetStdHandle",   SYS_GETSTDHANDLE, 1 },
    { "WriteFile",      SYS_WRITE,        5 },
    { "GetTickCount",   SYS_TICKS,        0 },
    { "WriteConsoleA",  SYS_WRITE,        5 },   /* ayni arguman duzeni: (h, buf, n, &yazilan, rezerv) */
    { "GetLastError",   SYS_LASTERR,      0 },
};

static void stub_emit(u8 *s, u32 num, u32 argc){
    static const u8 mv[5][4] = {
        { 0x8B, 0x5C, 0x24, 0x04 },   /* mov ebx, [esp+4]  */
        { 0x8B, 0x4C, 0x24, 0x08 },   /* mov ecx, [esp+8]  */
        { 0x8B, 0x54, 0x24, 0x0C },   /* mov edx, [esp+12] */
        { 0x8B, 0x74, 0x24, 0x10 },   /* mov esi, [esp+16] */
        { 0x8B, 0x7C, 0x24, 0x14 },   /* mov edi, [esp+20] */
    };
    u32 p = 0;
    s[p++] = 0xB8;                                        /* mov eax, imm32 */
    s[p++] = (u8)num; s[p++] = (u8)(num >> 8); s[p++] = (u8)(num >> 16); s[p++] = (u8)(num >> 24);
    for(u32 i = 0; i < argc; i++) for(u32 k = 0; k < 4; k++) s[p++] = mv[i][k];
    s[p++] = 0xCD; s[p++] = 0x80;                         /* int 0x80 */
    s[p++] = 0xC2; s[p++] = (u8)(argc * 4u); s[p++] = 0;  /* ret imm16 (stdcall) */
}

static void user_resolve_reset(void){ STUB_N = 0; }

/* pe_load'un import cozucusu: kernel32 adlarini stub adreslerine baglar */
static unsigned user_resolve(void *ctx, const char *dll, const char *name, unsigned ord){
    (void)ctx;
    if(!dll || !name || ord) return 0;
    if(!exe_name_is(dll, "kernel32.dll")) return 0;
    for(unsigned i = 0; i < sizeof SYSMAP / sizeof SYSMAP[0]; i++){
        if(!exe_name_is(name, SYSMAP[i].name)) continue;
        if(STUB_N >= STUB_MAX) return 0;
        stub_emit(STUB_BUF + STUB_N * STUB_SLOT, SYSMAP[i].num, SYSMAP[i].argc);
        u32 addr = UT_STUB + STUB_N * STUB_SLOT;
        STUB_N++;
        return addr;
    }
    /* Desteklenmeyen API: programin neden yuklenmedigini ekrana yaz (kullaniciya acik hata) */
    const char *pre = "EKSIK API: ";
    for(const char *c = pre; *c; c++) exe_putc(*c);
    for(const char *c = dll; *c; c++) exe_putc(*c);
    exe_putc('!');
    for(const char *c = name; *c; c++) exe_putc(*c);
    exe_putc('\n');
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Program hazirligi, calistirma, sonlandirma                               */
/* ------------------------------------------------------------------------ */
#define USER_HEAP_PAGES (UT_HEAP_SIZE / 4096u)
#define USER_STACK_PAGES (65536u / 4096u)

/* EXE_MEM'deki PE imajini, stub'lari, yigini ve heap'i yeni bir adres uzayina yerlestirir */
static int user_prepare(const pe_info *info){
    unsigned dir = pg_space_new();
    if(!dir) return -1;
    USER_DIR = dir;
    if(info->image_size == 0 || info->image_size > EXE_IMG_MAX) goto fail;

    unsigned pages = (info->image_size + 4095u) / 4096u;
    for(unsigned p = 0; p < pages; p++)
        if(!user_page(dir, UT_CODE + p * 4096u, PG_RW | PG_US, EXE_MEM + p * 4096u)) goto fail;
    if(!user_page(dir, UT_STUB, PG_RW | PG_US, STUB_BUF)) goto fail;
    for(unsigned s = 0; s < USER_STACK_PAGES; s++)
        if(!user_page(dir, UT_STACK_TOP - (s + 1u) * 4096u, PG_RW | PG_US, 0)) goto fail;
    for(unsigned h = 0; h < USER_HEAP_PAGES; h++)
        if(!user_page(dir, UT_HEAP + h * 4096u, PG_RW | PG_US, 0)) goto fail;

    USER_SP = UT_STACK_TOP - 16u;
    return 0;
fail:
    user_free(dir);
    USER_DIR = 0;
    return -1;
}

/* ring 3'e gecis: exe_call tarafindan cagrilir; donus yok, donus EXE_RET uzerinden user_finish ile olur */
__attribute__((noinline, used)) void user_start(void){
    pg_cr3(USER_DIR);
    __asm__ volatile(
        "movw $0x23, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "pushl $0x23\n\t"          /* ss */
        "pushl %0\n\t"             /* esp */
        "pushl $0x202\n\t"         /* eflags: IF=1 */
        "pushl $0x1B\n\t"          /* cs (ring 3) */
        "pushl %1\n\t"             /* eip */
        "iret\n\t"
        :: "r"(USER_SP), "r"(USER_ENTRY) : "eax", "memory");
    for(;;) { }
}

/* Programi bitirir: kullanici adres uzayi serbest, kernel yigini ve donus adresi geri yuklenir */
static void __attribute__((noreturn)) user_finish(u32 code){
    EXE_CODE = code;
    unsigned dir = USER_DIR;
    USER_DIR = 0;
    pg_cr3(PG_KDIR);
    if(dir) user_free(dir);
    __asm__ volatile(
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "sti\n\t"
        "movl %0, %%esp\n\t"
        "jmp *%1\n\t"
        :: "m"(EXE_ESP), "m"(EXE_RET) : "eax", "memory");
    for(;;) { }
}

/* Ring 3'te bir istisna (sayfa hatasi, gecersiz komut...): program sonlanir, cekirdek calismaya devam eder */
static void user_fault(u32 n, u32 cr2){
    char msg[TL_W], num[16], hx[12];
    if(EXE_LN > 0) exe_flush();
    itoa((int)n, num);
    app_str(msg, "EXE: PROGRAM COKTU (VEKTOR ", num);
    hex_str(cr2, 8, hx);
    char tail[TL_W];
    app_str(tail, ", ADRES 0x", hx);
    tl_cat(msg, tail);
    tl_cat(msg, ")");
    tl_add(msg);
    user_finish(0xFFFFFFFFu);
}

/* Kullanici girisi: yazmaclar ve yigin kaydedilir, user_start ring 3'e gecer.
   Program bittiginde (user_finish) buradaki etikete donulur ve yazmaclar geri yuklenir. */
static void exe_call(void){
    __asm__ volatile(
        "pushl %%ebx\n\t"
        "pushl %%esi\n\t"
        "pushl %%edi\n\t"
        "pushl %%ebp\n\t"
        "movl $1f, %0\n\t"
        "movl %%esp, %1\n\t"
        "call user_start\n"
        "1:\n\t"
        "popl %%ebp\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebx\n\t"
        : "=m"(EXE_RET), "=m"(EXE_ESP)
        :
        : "eax", "ecx", "edx", "memory", "cc");
}

/* "exe DOSYA": EXE_FILE'daki n baytlik PE imajini UT_CODE'a relocate eder, kullanici adres
   uzayini kurar ve ring 3'te calistirir. Kernel32 importlari stub'lara baglanir. */
static void exe_exec(unsigned n){
    pe_info info;
    unsigned entry = 0;
    user_resolve_reset();
    int e = pe_load(EXE_FILE, n, EXE_MEM, EXE_IMG_MAX, UT_CODE, user_resolve, 0, &info, &entry);
    if(e != PE_OK){ tl_add(pe_err_text(e)); return; }
    if(user_prepare(&info) != 0){ tl_add("EXE: BELLEK YETERSIZ"); return; }

    EXE_LN = 0;
    EXE_CODE = 0;
    USER_ENTRY = entry;
    exe_call();
    if(EXE_LN > 0) exe_flush();
    char msg[TL_W], part[TL_W], num[16];
    itoa((int)EXE_CODE, num);
    app_str(part, "EXE: PROGRAM BITTI (KOD ", num);
    app_str(msg, part, ")");
    tl_add(msg);
}
