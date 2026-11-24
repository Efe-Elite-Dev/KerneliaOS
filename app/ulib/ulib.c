/* KerneliaOS kullanici kutuphanesi uygulamasi. Bkz. ulib.h.
   Derleme (build_user.py): -nostdlib -fno-builtin -fno-tree-loop-distribute-patterns
   (bellek fonksiyonlarinin kendi kendini cagirmasini onler). */
#include "../userabi.h"
#include "../heap.c"
#include "ulib.h"

/* int 0x80: eax = numara, ebx/ecx/edx/esi/edi = arglar, donus eax (userabi.h) */
static inline unsigned int ul_sys(unsigned int num, unsigned int a1, unsigned int a2,
                                  unsigned int a3, unsigned int a4, unsigned int a5){
    unsigned int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

void ul_exit(unsigned int code){
    ul_sys(SYS_EXIT, code, 0, 0, 0, 0);
    for(;;) { }
}

unsigned int ul_ticks(void){ return ul_sys(SYS_TICKS, 0, 0, 0, 0, 0); }

int ul_write(const void *buf, unsigned int n){
    /* 0x101: GetStdHandle(-11) ile ayni sahte standart cikti tutamaci */
    return (int)ul_sys(SYS_WRITE, 0x101u, (unsigned int)(unsigned long)buf, n, 0, 0);
}

void *memcpy(void *dst, const void *src, unsigned int n){
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while(n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned int n){
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if(d == s || n == 0) return dst;
    if(d < s){ while(n--) *d++ = *s++; }
    else { d += n; s += n; while(n--) *--d = *--s; }
    return dst;
}

void *memset(void *dst, int c, unsigned int n){
    unsigned char *d = (unsigned char *)dst;
    while(n--) *d++ = (unsigned char)c;
    return dst;
}

static int ul_heap_ready = 0;

void *malloc(unsigned int n){
    if(!ul_heap_ready){
        if(heap_init((void *)(unsigned long)UT_HEAP, UT_HEAP_SIZE) != 0) return 0;
        ul_heap_ready = 1;
    }
    return kmalloc(n);
}

void free(void *p){ kfree(p); }

unsigned int strlen(const char *s){
    unsigned int n = 0;
    while(s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b){
    while(*a && *a == *b){ a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *dst, const char *src){
    char *d = dst;
    while((*d++ = *src++)) { }
    return dst;
}

/* printf: tampon 256 bayt; dolunca yazilir */
struct ul_out { char buf[256]; unsigned int n; };

static void out_putc(struct ul_out *o, char c){
    if(o->n >= sizeof o->buf) { ul_write(o->buf, o->n); o->n = 0; }
    o->buf[o->n++] = c;
}

static void out_utoa(struct ul_out *o, unsigned int v, unsigned int base){
    char t[12];
    int k = 0;
    const char *digits = "0123456789abcdef";
    do { t[k++] = digits[v % base]; v /= base; } while(v);
    while(k) out_putc(o, t[--k]);
}

int ul_printf(const char *fmt, ...){
    struct ul_out o;
    o.n = 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for(const char *p = fmt; *p; p++){
        if(*p != '%'){ out_putc(&o, *p); continue; }
        p++;
        if(*p == 0) break;
        switch(*p){
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if(!s) s = "(null)";
            while(*s) out_putc(&o, *s++);
            break;
        }
        case 'd': {
            int v = __builtin_va_arg(ap, int);
            if(v < 0){ out_putc(&o, '-'); out_utoa(&o, (unsigned int)(-(v + 1)) + 1u, 10); }
            else out_utoa(&o, (unsigned int)v, 10);
            break;
        }
        case 'u': out_utoa(&o, __builtin_va_arg(ap, unsigned int), 10); break;
        case 'x': out_utoa(&o, __builtin_va_arg(ap, unsigned int), 16); break;
        case 'c': out_putc(&o, (char)__builtin_va_arg(ap, int)); break;
        case '%': out_putc(&o, '%'); break;
        default:  out_putc(&o, '%'); out_putc(&o, *p); break;
        }
    }
    __builtin_va_end(ap);
    if(o.n) ul_write(o.buf, o.n);
    return 0;
}
