/* WindOS seri port (COM1, 0x3F8): gunluk ve test ciktisi. QEMU'da -serial ile yakalanir.
   kernel.c icinde include edilir; inb/outb once tanimli olmali.
   Ayrica QEMU isa-debug-exit (port 0xF4): test calistiricisinin cikis kodunu okumasi icin. */
#ifndef WIND_SERIAL_C
#define WIND_SERIAL_C

#define SER_PORT 0x3F8
static unsigned SER_OK = 0;      /* ser_init basarili (port yanit verdi) */

/* 115200 baud, 8N1, FIFO acik. Donanim yoksa bile cokmez: yazma beklemesi zaman asimli. */
static void ser_init(void){
    outb(SER_PORT + 1, 0x00);    /* kesmeler kapali */
    outb(SER_PORT + 3, 0x80);    /* DLAB ac */
    outb(SER_PORT + 0, 0x01);    /* bolen 1 -> 115200 */
    outb(SER_PORT + 1, 0x00);
    outb(SER_PORT + 3, 0x03);    /* 8 bit, parite yok, 1 stop; DLAB kapali */
    outb(SER_PORT + 2, 0xC7);    /* FIFO ac, temizle, 14 bayt esik */
    outb(SER_PORT + 4, 0x0B);    /* DTR, RTS, OUT2 */
    SER_OK = 1;
}

static void ser_putc(char c){
    if(!SER_OK) return;
    for(u32 t = 0; t < 100000 && !(inb(SER_PORT + 5) & 0x20); t++) ;
    outb(SER_PORT, (u8)c);
}

static void ser_puts(const char *s){
    for(; *s; s++){
        if(*s == '\n') ser_putc('\r');
        ser_putc(*s);
    }
}

/* v'yi digits basamakli onaltilik olarak yazar (0x yok) */
static void ser_hex(u32 v, int digits){
    static const char hx[] = "0123456789ABCDEF";
    for(int i = digits - 1; i >= 0; i--) ser_putc(hx[(v >> (i * 4)) & 0xF]);
}

/* QEMU isa-debug-exit: cikis kodu (v << 1) | 1. v=0 -> 1, v=1 -> 3, v=2 -> 5.
   Gercek donanimda 0xF4 genelde bos bir porttur; yine de yalnizca test derlemesinde cagrilir. */
static inline void dbg_exit(u32 v){
    outb(0xF4, (u8)v);
    for(;;) __asm__ volatile("cli; hlt");
}

#endif
