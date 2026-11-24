/* KerneliaOS zamanlayici: IRQ0 (PIT) kesmesinde baglam degistiren round-robin.
   timer_stub (asm) butun genel yazmaclari yigina koyar, sched_tick'e yigin isaretcisini
   verir; donen yeni yigin isaretcisiyle popa/iret yapar. Gorev yigini kmalloc ile ayrilir.
   kernel.c icinde include edilir; TICKS, idt_gate, kmalloc/kfree (mem.c) once tanimli olmali. */

#define TASK_MAX   8
#define TASK_STACK (16u * 1024u)

typedef struct { u32 esp; u8 *stack; u8 used; u8 dead; } Task;
static Task TASK[TASK_MAX];
static int TASK_CUR = 0;       /* su an calisan gorev (kesmede kaydedilen) */
static int TASK_N = 0;         /* calisabilir gorev sayisi */
static volatile u32 BG_COUNT = 0;   /* arka plan gorevinin ilerleme sayaci */

static void task_exit(void);

/* IRQ0 girisi. Yazmaclar yigina itilir, sched_tick yeni esp doner, EOI gonderilir. */
__asm__(
    ".text\n"
    ".globl timer_stub\n"
    "timer_stub:\n"
    "    pusha\n"
    "    pushl %esp\n"
    "    call sched_tick\n"
    "    addl $4, %esp\n"
    "    movl %eax, %esp\n"
    "    movb $0x20, %al\n"
    "    outb %al, $0x20\n"
    "    popa\n"
    "    iret\n"
);

/* Zamanlayici kesmesi: TICK sayaci artar; gerekiyorsa baska bir gorevin esp'si doner */
u32 sched_tick(u32 esp){
    TICKS++;
    int old = TASK_CUR;
    TASK[old].esp = esp;
    int nxt = old;
    for(int k = 1; k <= TASK_MAX; k++){
        int c = (old + k) % TASK_MAX;
        if(TASK[c].used){ nxt = c; break; }
    }
    /* Olu bir gorevin yigini, uzerinde calisilmadigi bir kesmede serbest birakilir.
       old su an kesmedeki yiginda calisiyor; onu hemen bosaltmak guvensiz. */
    for(int j = 0; j < TASK_MAX; j++){
        if(j != old && TASK[j].dead && TASK[j].stack){
            kfree(TASK[j].stack);
            TASK[j].stack = 0;
            TASK[j].dead = 0;
        }
    }
    TASK_CUR = nxt;
    return TASK[nxt].esp;
}

/* Ana gorev (kernel_main) gorev 0'dir; yigini zaten kullaniliyor */
static void sched_init(void){
    for(int i = 0; i < TASK_MAX; i++){ TASK[i].used = 0; TASK[i].dead = 0; TASK[i].stack = 0; }
    TASK[0].used = 1;
    TASK_CUR = 0;
    TASK_N = 1;
}

/* Yeni gorev: fn bir kez calisir; donerse task_exit'e gider. Donus: gorev no, -1 hata. */
static int task_create(void (*fn)(void)){
    int i = 1;
    while(i < TASK_MAX && TASK[i].used) i++;
    if(i >= TASK_MAX) return -1;
    u8 *stk = (u8 *)kmalloc(TASK_STACK);
    if(!stk) return -1;

    u32 top = ((u32)(unsigned long)stk + TASK_STACK) & ~15u;
    u32 R = top - 4;                          /* fn bittiginde donulecek adres (esp = R) */
    u16 cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    /* iret yigini (R'den asagi): eflags, cs, eip; sonra popa icin 8 sifir */
    #define TW(off) (*(volatile u32 *)(unsigned long)(R - (off)))
    TW(0)  = (u32)(unsigned long)task_exit;
    TW(4)  = 0x202u;                          /* eflags: IF=1 */
    TW(8)  = (u32)cs;
    TW(12) = (u32)(unsigned long)fn;          /* eip */
    for(u32 k = 0; k < 8; k++) TW(16 + 4 * k) = 0;
    #undef TW

    TASK[i].esp = R - 44;                     /* popa'nin baslangici: edi */
    TASK[i].stack = stk;
    TASK[i].used = 1;
    TASK[i].dead = 0;
    TASK_N++;
    return i;
}

/* Gorev bitti: calisabilir sayisindan dusulur, yigin sonraki kesmede serbest kalir */
static void task_exit(void){
    int c = TASK_CUR;
    TASK[c].used = 0;
    TASK[c].dead = 1;
    TASK_N--;
    for(;;) __asm__ volatile("hlt");
}

/* Arka plan gorevi: her turda sayaci artirir ve bir sonraki kesmeye kadar uyur */
static void bg_task(void){
    for(;;){
        BG_COUNT++;
        __asm__ volatile("hlt");
    }
}

/* "gorev" komutu: gorev ve bellek durumunu terminale yazar */
static void sys_status(void){
    char a[TL_W], n[16];
    dec_str((u32)TASK_N, n);
    app_str(a, "GOREVLER: CALISABILIR ", n);
    tl_add(a);
    dec_str(BG_COUNT, n);
    app_str(a, "  ARKA PLAN SAYACI: ", n);
    tl_add(a);
    dec_str(FRAME_FREE_N / 256u, n);
    app_str(a, "  BOS BELLEK (MB): ", n);
    tl_add(a);
    dec_str(heap_free_total(), n);
    app_str(a, "  HEAP BOS (BAYT): ", n);
    tl_add(a);
}
