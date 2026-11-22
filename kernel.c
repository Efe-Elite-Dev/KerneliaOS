/*
 * WindOS - kernel.c  v18.1 (hizli surum)
 * Gelistirici: Claude Efe-Elite-Dev  |  Asistan: Claude
 *
 * v18.0'a gore degisiklikler:
 *  - Piksel basina cizim yerine satir bazli dolgu ve daire taramasi
 *  - Kare yalnizca bir sey degisince (ya da bir uygulama canliyken) cizilir;
 *    bostayken islemci hlt ile uyur
 *  - Arka tampon kopyasi 32-bit satirlar halinde yapilir
 *  - Tiklama kenari kare sonunda tuketilir (tek tik birden fazla tetiklemez)
 */
#include "kernel.h"

/* Disk (ATA PIO), FAT12/16 okuyucu ve .wind ayristiricisi: app/ klasorunden, tek derleme biriminde */
#include "app/ata.c"
#include "app/fat.c"
#include "app/wind.c"
#include "app/pe.c"

typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef int            i32;
typedef signed char    i8;
#define NULL ((void*)0)

/* ---- Ekran ---- */
static volatile u8 *FB = (u8*)0;
static u32 SW = 1024, SH = 768;
static u32 PITCH = 4096;
static u32 BPP = 4;
#define MAX_SW 1920
#define MAX_SH 1080
static u32 BB[MAX_SW * MAX_SH];          /* arka tampon, 0xAARRGGBB */

static int DIRTY = 1;                    /* bu karede yeniden cizim gerekli */
#define FRAME_HZ 50
static volatile u32 TICKS = 0;           /* zamanlayici tick sayaci (FRAME_HZ/sn) */

/* ---- Kurulum: ilk acilista kullanici adi ---- */
static int  SETUP_DONE = 0;
static char SETUP_BUF[24];
static int  SETUP_LEN = 0;
static char USER_NAME[24] = "kullanici";
static char PROMPT_STR[48] = "kullanici@WINDOS:~$ ";
static int ANIM_PREV = 0;                /* onceki karede animasyon vardi */
static u32 SYS_RAM_MB = 0;

/* ---- Acilis (AI tarama) ekrani ---- */
static int AI_BOOT_PHASE = 0;
static int AI_TIMER = 0;

/* ---- Uygulama durumlari ---- */
static int START_MENU_OPEN = 0;
static int SPOTLIGHT_OPEN = 0;
static int TASKMGR_OPEN = 0;
static int TERM_OPEN = 0;
static int BR_OPEN = 0;
static int FO = 0, DISP_OPEN = 0, SYS_OPEN = 0, AI_OPEN = 0;
static int MUS_OPEN = 0, MIKU_OPEN = 0, NOTE_OPEN = 0, SN_OPEN = 0;
/* Klavye odagi: 0 yok, 1 terminal, 2 not defteri, 3 yilan, 4 tarayici */
static int FOCUS = 0;
static i32 FX = 100, FY = 80, FD = 0, FDX = 0, FDY = 0;
static i32 CX = 150, CY = 100, CD = 0, CDX = 0, CDY = 0;


/* ---- Renk paleti ---- */
#define CW       0xFFFFFFFFu
#define CK       0xFF000000u
#define BG_BASE  0xFF0A0C10u
#define DOCK_BG  0xDD121214u
#define PAN_BG   0xFF1C1C1Eu
#define PAN_BD   0xFF3A3A40u
#define SIDEBAR  0xFF121214u
#define CTXT     0xFFF3F4F6u
#define CGY      0xFFA1AAB7u
#define WIN_BLUE 0xFF0078D4u
#define AI_PURP  0xFF8E44ADu
#define CHR_GRN  0xFF2ECC71u
#define COR      0xFFF39C12u
#define CRD      0xFFE74C3Cu
#define XUB_BLU  0xFF2980B9u
#define SHADOW   0xFF030303u
#define AND_GRN  0xFF3DDC84u
#define DEB_ORG  0xFFE95420u
#define CGN      0xFF27AE60u
#define LIN_ORG  0xFFE67E22u
#define SAFE_BLK 0xFF080808u
#define MAC_YEL  0xFFF1C40Fu

/* ---- I/O portlari ---- */
static inline u8   inb (u16 p)       { u8 v; __asm__ volatile("inb  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }

/* ---- Yardimcilar ---- */
void* memcpy(void* dest, const void* src, u32 n) {
    u8* d = (u8*)dest; const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dest;
}
static u32 klen(const char *s){ u32 n = 0; while(s[n]) n++; return n; }
static void kcpy(char *d, const char *s){ while(*s) *d++ = *s++; *d = 0; }
static void itoa(int n, char s[]) {
    int i = 0, sign = n;
    if(sign < 0) n = -n;
    do { s[i++] = n % 10 + '0'; } while((n /= 10) > 0);
    if(sign < 0) s[i++] = '-';
    s[i] = '\0';
    for(int j = 0, k = i - 1; j < k; j++, k--){ char t = s[j]; s[j] = s[k]; s[k] = t; }
}
static int has_prefix(const char *s, const char *p) {
    while(*p){ if(*s != *p) return 0; s++; p++; }
    return 1;
}
static i32 isqrt_i(i32 v){ i32 r = 0; while((r + 1) * (r + 1) <= v) r++; return r; }

/* ---- Font (8x8, bit0 en sol piksel) ---- */
static const u8 F8[256][8]={
 [' ']={0,0,0,0,0,0,0,0},['!']={0x18,0x3C,0x3C,0x18,0x18,0,0x18,0},['"']={0x36,0x36,0,0,0,0,0,0},['#']={0x36,0x7F,0x36,0x36,0x7F,0x36,0x36,0},
 ['$']={0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0},['%']={0x63,0x33,0x18,0x0C,0x66,0x63,0,0},['&']={0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0},['\'']={0x06,0x0C,0,0,0,0,0,0},
 ['(']={0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0},[')']={0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0},['*']={0x66,0x3C,0xFF,0x3C,0x66,0,0,0},['+']={0,0x0C,0x0C,0x3F,0x0C,0x0C,0,0},
 [',']={0,0,0,0,0,0x18,0x18,0x0C},['-']={0,0,0,0x3F,0,0,0,0},['.']={0,0,0,0,0,0x18,0x18,0},['/']={0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0},
 ['0']={0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0},['1']={0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0},['2']={0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0},['3']={0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0},
 ['4']={0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0},['5']={0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0},['6']={0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0},['7']={0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0},
 ['8']={0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0},['9']={0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0},[':']={0,0x18,0x18,0,0x18,0x18,0,0},[';']={0,0x18,0x18,0,0x18,0x18,0x0C,0},
 ['<']={0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0},['=']={0,0x3F,0,0,0x3F,0,0,0},['>']={0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0},['?']={0x1E,0x33,0x30,0x18,0x0C,0,0x0C,0},
 ['@']={0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0},['A']={0x0C,0x1E,0x33,0x3F,0x33,0x33,0x33,0},['B']={0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0},['C']={0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0},
 ['D']={0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0},['E']={0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0},['F']={0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0},['G']={0x3C,0x66,0x03,0x73,0x63,0x66,0x7C,0},
 ['H']={0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0},['I']={0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0},['J']={0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0},['K']={0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0},
 ['L']={0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0},['M']={0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0},['N']={0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0},['O']={0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0},
 ['P']={0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0},['Q']={0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0},['R']={0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0},['S']={0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0},
 ['T']={0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0},['U']={0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0},['V']={0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0},['W']={0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0},
 ['X']={0,0x63,0x36,0x1C,0x1C,0x36,0x63,0},['Y']={0,0x33,0x33,0x33,0x3E,0x30,0x33,0x1E},['Z']={0,0x3F,0x19,0x0C,0x26,0x3F,0,0},['{']={0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0},
 ['|']={0x18,0x18,0x18,0,0x18,0x18,0x18,0},['}']={0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0},['~']={0x6E,0x3B,0,0,0,0,0,0},['_']={0,0,0,0,0,0,0,0x7F},
/* Turkce harfler (app/keymap_tr.h): buyuk harf glifleri; kucuk ve buyuk kodlar ayni gorunur */
[0x80]={0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x0C}, [0x81]={0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x0C},   /* c / C-cedilla */
[0x82]={0x12,0x3C,0x66,0x03,0x73,0x63,0x66,0x7C}, [0x83]={0x12,0x3C,0x66,0x03,0x73,0x63,0x66,0x7C},   /* g / G-breve */
[0x84]={0x36,0x1C,0x36,0x63,0x63,0x63,0x36,0x1C}, [0x85]={0x36,0x1C,0x36,0x63,0x63,0x63,0x36,0x1C},   /* o / O-umlaut */
[0x86]={0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x0C}, [0x87]={0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x0C},   /* s / S-cedilla */
[0x88]={0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x3F}, [0x89]={0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x3F},   /* u / U-umlaut */
[0x8A]={0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0},   [0x8B]={0x0C,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E}    /* dotless i / I-dot */
};

/* ========================================================================= */
/* CIZIM: satir bazli dolgu, daire ve yuvarlak dikdortgen                     */
/* ========================================================================= */
static inline void pset(i32 x, i32 y, u32 c){
    if((u32)x < SW && (u32)y < SH) BB[(u32)y * SW + (u32)x] = c;
}

/* Dikdortgen dolgu (ekran disina tasan kisim kirpilir) */
static void fr(i32 x, i32 y, i32 w, i32 h, u32 c){
    if(w <= 0 || h <= 0) return;
    i32 x1 = x < 0 ? 0 : x, y1 = y < 0 ? 0 : y;
    i32 x2 = x + w > (i32)SW ? (i32)SW : x + w;
    i32 y2 = y + h > (i32)SH ? (i32)SH : y + h;
    if(x2 <= x1 || y2 <= y1) return;
    for(i32 j = y1; j < y2; j++){
        u32 *p = BB + (u32)j * SW;
        for(i32 i = x1; i < x2; i++) p[i] = c;
    }
}

/* Yari saydam dolgu: mevcut piksel ile %50 karistirilir */
static void frg(i32 x, i32 y, i32 w, i32 h, u32 c){
    if(w <= 0 || h <= 0) return;
    i32 x1 = x < 0 ? 0 : x, y1 = y < 0 ? 0 : y;
    i32 x2 = x + w > (i32)SW ? (i32)SW : x + w;
    i32 y2 = y + h > (i32)SH ? (i32)SH : y + h;
    if(x2 <= x1 || y2 <= y1) return;
    u32 cc = (c & 0xFEFEFEu) >> 1;
    for(i32 j = y1; j < y2; j++){
        u32 *p = BB + (u32)j * SW;
        for(i32 i = x1; i < x2; i++) p[i] = ((p[i] & 0xFEFEFEu) >> 1) + cc;
    }
}

static void rb(i32 x, i32 y, i32 w, i32 h, u32 c, i32 t){
    fr(x, y, w, t, c); fr(x, y + h - t, w, t, c);
    fr(x, y, t, h, c); fr(x + w - t, y, t, h, c);
}

/* Dolu daire: (dx,dy) r yaricapinda ise dolu (eski piksel testiyle ayni) */
static void disc(i32 cx, i32 cy, i32 r, u32 c){
    if(r <= 0) return;
    for(i32 dy = -r; dy <= r; dy++){
        i32 hw = isqrt_i(r * r - dy * dy);
        fr(cx - hw, cy + dy, 2 * hw + 1, 1, c);
    }
}

/* Yuvarlak koseli dikdortgen; glass=1 ise yari saydam */
static void rr_impl(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c, int glass){
    if(w <= 0 || h <= 0) return;
    if(r > w / 2) r = w / 2;
    if(r > h / 2) r = h / 2;
    for(i32 j = 0; j < h; j++){
        i32 ins = 0;
        if(j < r){ i32 dy = j - r; ins = r - isqrt_i(r * r - dy * dy); }
        else if(j >= h - r){ i32 dy = j - (h - r - 1); ins = r - isqrt_i(r * r - dy * dy); }
        if(glass) frg(x + ins, y + j, w - 2 * ins, 1, c);
        else      fr (x + ins, y + j, w - 2 * ins, 1, c);
    }
}
static void rr (i32 x, i32 y, i32 w, i32 h, i32 r, u32 c){ rr_impl(x, y, w, h, r, c, 0); }
static void rrg(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c){ rr_impl(x, y, w, h, r, c, 1); }

/* Harf: bg kullanilmaz (seffaf). sc = olcek */
static void dc(i32 x, i32 y, char ch, u32 fg, u32 bg, i32 sc){
    (void)bg;
    if(ch >= 'a' && ch <= 'z') ch -= 32;
    if((u8)ch >= 0x8C) ch = '?';   /* 0x80-0x8B: Turkce harf glifleri */
    const u8 *g = F8[(u8)ch];
    for(i32 row = 0; row < 8; row++){
        u8 bits = g[row];
        if(!bits) continue;
        for(i32 col = 0; col < 8; col++){
            if(!(bits & (1<<col))) continue;   /* font8x8: bit0 = en sol piksel */
            if(sc == 1) pset(x + col, y + row, fg);
            else        fr(x + col * sc, y + row * sc, sc, sc, fg);
        }
    }
}
static void ds(i32 x, i32 y, const char *s, u32 fg, u32 bg, i32 sc){
    while(*s){
        if(*s == '\n'){ x = 0; y += 8 * sc + 2; }
        else { dc(x, y, *s, fg, bg, sc); x += 8 * sc; }
        s++;
    }
}
static void dsc(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, i32 sc){
    i32 tw = (i32)klen(s) * 8 * sc;
    if(tw < w) ds(x + (w - tw) / 2, y, s, fg, bg, sc);
    else       ds(x, y, s, fg, bg, sc);
}

/* Arka tampon -> ekran. Ekran kopyasi 32-bit satirlar halinde yapilir. */
static void swap_buffers(void){
    if(BPP == 4 && PITCH == SW * 4){
        for(u32 y = 0; y < SH; y++){
            volatile u32 *d = (volatile u32 *)(FB + y * PITCH);
            const u32 *s = BB + y * SW;
            for(u32 x = 0; x < SW; x++) d[x] = s[x];
        }
        return;
    }
    for(u32 y = 0; y < SH; y++){
        u32 fb_row = y * PITCH;
        for(u32 x = 0; x < SW; x++){
            u32 color = BB[y * SW + x];
            u32 off = fb_row + x * BPP;
            if(BPP == 3){
                FB[off]     = color & 0xFF;
                FB[off + 1] = (color >> 8) & 0xFF;
                FB[off + 2] = (color >> 16) & 0xFF;
            } else if(BPP == 2){
                u16 r = ((color >> 16) & 0xFF) >> 3;
                u16 g = ((color >> 8) & 0xFF) >> 2;
                u16 b = (color & 0xFF) >> 3;
                *(volatile u16 *)(FB + off) = (u16)((r << 11) | (g << 5) | b);
            }
        }
    }
}

/* ========================================================================= */
/* KLAVYE & FARE                                                             */
/* ========================================================================= */
#include "app/keymap_tr.h"   /* Turkce Q klavye tablosu: TR_KEYS[tarama kodu][normal/shift] */
static u8 K_SH = 0, K_CP = 0, K_ALT = 0;

/* PS/2 veri portu (0x60) tek yerden okunur; baytlar klavye/fare kuyruguna ayrilir */
static u8 KQ[64], MQ[64];
static int KQH = 0, KQT = 0, MQH = 0, MQT = 0;
static void ps2_pump(void){
    for(int n = 0; n < 64; n++){
        u8 st = inb(0x64);
        if(!(st & 0x01)) break;
        u8 d = inb(0x60);
        if(st & 0x20){ int nt = (MQT + 1) & 63; if(nt != MQH){ MQ[MQT] = d; MQT = nt; } }
        else         { int nt = (KQT + 1) & 63; if(nt != KQH){ KQ[KQT] = d; KQT = nt; } }
    }
}
static int kq_pop(u8 *d){ if(KQH == KQT) return 0; *d = KQ[KQH]; KQH = (KQH + 1) & 63; return 1; }
static int mq_pop(u8 *d){ if(MQH == MQT) return 0; *d = MQ[MQH]; MQH = (MQH + 1) & 63; return 1; }

/* Ok tuslari: Turkce harf kodlarindan (0x80-0x8B) ayri bir aralikta */
#define KEY_UP    0xF1
#define KEY_DOWN  0xF2
#define KEY_LEFT  0xF3
#define KEY_RIGHT 0xF4

/* Bir tus olayi dondurur: ASCII, ok kodu veya 0 (olay yok) */

static u8 kbd_poll(void){
    static int ext = 0;
    u8 sc; if(!kq_pop(&sc)) return 0;
    if(sc == 0xE0){ ext = 1; return 0; }
    if(sc == 0x38){ K_ALT = 1; ext = 0; return 0; }
    if(sc == 0xB8){ K_ALT = 0; ext = 0; return 0; }
    if(sc & 0x80){
        u8 r = sc & 0x7F;
        if(r == 0x2A || r == 0x36) K_SH = 0;
        ext = 0; return 0;
    }
    if(ext){
        ext = 0;
        if(sc == 0x48) return KEY_UP;
        if(sc == 0x50) return KEY_DOWN;
        if(sc == 0x4B) return KEY_LEFT;
        if(sc == 0x4D) return KEY_RIGHT;
        return 0;
    }
    if(sc == 0x2A || sc == 0x36){ K_SH = 1; return 0; }
    if(sc == 0x3A){ K_CP = !K_CP; return 0; }
    if(sc >= 128) return 0;
    u8 lo = TR_KEYS[sc][0], up = TR_KEYS[sc][1];
    if(!lo) return 0;
    /* Harf tuslari CapsLock ile de buyur; noktalama yalnizca Shift ile degisir */
    int letter = (lo >= 'a' && lo <= 'z') || (lo >= 0x80 && lo <= 0x8B);
    return (letter ? (K_SH ^ K_CP) : K_SH) ? up : lo;
}

static i32 MX = 512, MY = 384, MLB = 0, MRB = 0, PMLB = 0;
static u8  MCY = 0; static i8 MBF[3] = {0}; static int MOUSE_READY = 0;
static void m_cmd_wait(void){ u32 t = 100000; while(t-- && (inb(0x64) & 0x02)); }
static void m_dat_wait(void){ u32 t = 100000; while(t-- && !(inb(0x64) & 0x01)); }
static void m_write(u8 v){ m_cmd_wait(); outb(0x64, 0xD4); m_cmd_wait(); outb(0x60, v); }
static u8   m_read (void){ m_dat_wait(); return inb(0x60); }
static void mouse_init(void){
    m_cmd_wait(); outb(0x64, 0xA8);
    m_cmd_wait(); outb(0x64, 0x20); m_dat_wait();
    u8 cfg = inb(0x60); cfg |= 0x02; cfg &= ~0x20;
    m_cmd_wait(); outb(0x64, 0x60); m_cmd_wait(); outb(0x60, cfg);
    m_write(0xFF); m_read(); m_read(); m_read();
    m_write(0xF6); m_read(); m_write(0xF4); m_read();
    MOUSE_READY = 1;
}

/* Fare paketlerini isler. PMLB burada guncellenmez: kare sonunda tuketilir. */
static void mouse_poll(void){
    if(!MOUSE_READY) return;
    i32 ox = MX, oy = MY, ob = MLB, orb = MRB;
    int safety = 256;
    while(safety--){
        u8 dat; if(!mq_pop(&dat)) break;
        switch(MCY){
            case 0: if(!(dat & 0x08)){ MCY = 0; continue; } MBF[0] = (i8)dat; MCY = 1; break;
            case 1: MBF[1] = (i8)dat; MCY = 2; break;
            case 2:
                MBF[2] = (i8)dat; MCY = 0;
                if((MBF[0] & 0x40) || (MBF[0] & 0x80)) break;
                {
                    i32 dx = (i32)MBF[1], dy = (i32)MBF[2];
                    if(MBF[0] & 0x10) dx |= (i32)0xFFFFFF00;
                    if(MBF[0] & 0x20) dy |= (i32)0xFFFFFF00;
                    MX += dx; MY -= dy;
                    if(MX < 0) MX = 0;
                    if(MY < 0) MY = 0;
                    if(MX >= (i32)SW) MX = (i32)SW - 1;
                    if(MY >= (i32)SH) MY = (i32)SH - 1;
                    MLB = (MBF[0] & 0x01) ? 1 : 0;
                    MRB = (MBF[0] & 0x02) ? 1 : 0;
                }
                break;
        }
    }
    if(MX != ox || MY != oy || MLB != ob || MRB != orb) DIRTY = 1;
}

/* Tiklama: bu kare basildi mi (kenar) */
static int CLK(i32 x, i32 y, i32 w, i32 h){ return MLB && !PMLB && MX >= x && MX < x + w && MY >= y && MY < y + h; }
static int HOV(i32 x, i32 y, i32 w, i32 h){ return MX >= x && MX < x + w && MY >= y && MY < y + h; }
static void CUR(void){
    static const u8 cur[13][9] = {
        {1,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0},{1,2,2,1,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0},{1,2,2,2,2,1,0,0,0},{1,2,2,2,2,2,1,0,0},{1,2,2,2,2,2,2,1,0},
        {1,2,2,2,2,2,2,2,1},{1,2,2,2,2,1,1,1,1},{1,2,2,1,2,2,1,0,0},{1,2,1,0,1,2,2,1,0},
        {1,1,0,0,1,2,2,1,0}
    };
    for(int r = 0; r < 13; r++) for(int c = 0; c < 9; c++){
        if(cur[r][c] == 1) pset(MX + c, MY + r, CW);
        else if(cur[r][c] == 2) pset(MX + c, MY + r, CK);
    }
}

/* ========================================================================= */
/* DOSYA GEZGINI (sahte dosya listesi)                                       */
/* ========================================================================= */


/* Pencere cercevesi, baslik, kapatma dugmesi (tiklanirsa uygulama kapanir) */
static void DRAW_WINDOW(i32 x, i32 y, i32 w, i32 h, const char *title, u32 b_col){
    fr(x + 8, y + 8, w, h, SHADOW);
    rrg(x, y, w, h, 12, b_col);
    rb(x, y, w, h, PAN_BD, 1);
    fr(x, y + 35, w, 1, PAN_BD);
    dsc(x, y + 15, w, title, CTXT, 0, 1);
    disc(x + 20, y + 17, 6, CRD);
    disc(x + 40, y + 17, 6, MAC_YEL);
    disc(x + 60, y + 17, 6, CGN);
    if(CLK(x + 14, y + 11, 12, 12)){
        if(has_prefix(title, "DOSYA"))   FO = 0;
        if(has_prefix(title, "WIND-TERM")) TERM_OPEN = 0;
        if(has_prefix(title, "SISTEM"))  SYS_OPEN = 0;
        if(has_prefix(title, "GOREV"))   TASKMGR_OPEN = 0;
        if(has_prefix(title, "EKRAN"))   DISP_OPEN = 0;
        if(has_prefix(title, "WINDAI"))  AI_OPEN = 0;
    }
}

static void TASKMGR_APP(void){
    if(!TASKMGR_OPEN) return;
    DRAW_WINDOW(300, 150, 400, 220, "GOREV YONETICISI", PAN_BG);
    char b[48];
    kcpy(b, "CALISMA SURESI: ");
    itoa((int)(TICKS / FRAME_HZ), b + klen(b));
    kcpy(b + klen(b), " SN");
    ds(320, 200, b, CW, 0, 1);
    kcpy(b, "BELLEK: ");
    if(SYS_RAM_MB){ itoa((int)SYS_RAM_MB, b + klen(b)); kcpy(b + klen(b), " MB"); }
    else kcpy(b + klen(b), "BILINMIYOR");
    ds(320, 230, b, CW, 0, 1);
    ds(320, 280, "CPU kullanimi henuz olculmuyor.", CGY, 0, 1);
}

static void START_MENU(void){
    if(!START_MENU_OPEN) return;
    i32 sm_w = 400, sm_h = 500;
    i32 sm_x = ((i32)SW - sm_w) / 2;
    i32 sm_y = (i32)SH - 80 - sm_h - 10;
    rrg(sm_x, sm_y, sm_w, sm_h, 15, DOCK_BG);
    rb(sm_x, sm_y, sm_w, sm_h, PAN_BD, 1);
    dsc(sm_x, sm_y + 20, sm_w, "SABITLENMIS UYGULAMALAR", CW, 0, 1);

    rr(sm_x + 50, sm_y + 70, 60, 60, 12, WIN_BLUE);
    ds(sm_x + 55, sm_y + 140, "DOSYALAR", CTXT, 0, 1);
    if(CLK(sm_x + 50, sm_y + 70, 60, 60)){ FO = 1; START_MENU_OPEN = 0; }

    rr(sm_x + 170, sm_y + 70, 60, 60, 12, LIN_ORG);
    ds(sm_x + 175, sm_y + 140, "TERMINAL", CTXT, 0, 1);
    if(CLK(sm_x + 170, sm_y + 70, 60, 60)){ TERM_OPEN = 1; FOCUS = 1; START_MENU_OPEN = 0; }

    rr(sm_x + 290, sm_y + 70, 60, 60, 12, CRD);
    ds(sm_x + 290, sm_y + 140, "GOREV YON", CTXT, 0, 1);
    if(CLK(sm_x + 290, sm_y + 70, 60, 60)){ TASKMGR_OPEN = 1; START_MENU_OPEN = 0; }

}

static void FILEMGR(void);   /* dosya yoneticisi: depolama bolumunde tanimli */

static void DISPLAY_APP(void){
    if(!DISP_OPEN) return;
    DRAW_WINDOW(280, 120, 450, 200, "EKRAN BILGISI", PAN_BG);
    char b[48];
    kcpy(b, "COZUNURLUK: ");
    itoa((int)SW, b + klen(b)); kcpy(b + klen(b), " x "); itoa((int)SH, b + klen(b));
    ds(300, 170, b, CW, 0, 1);
    kcpy(b, "RENK DERINLIGI: ");
    itoa((int)BPP * 8, b + klen(b)); kcpy(b + klen(b), " BIT");
    ds(300, 195, b, CW, 0, 1);
}

static void SYSTEM_APP(void){
    if(!SYS_OPEN) return;
    DRAW_WINDOW(250, 150, 500, 300, "SISTEM BILGISI", PAN_BG);
    ds(280, 200, "ISLETIM SISTEMI: WINDOS 18.1", WIN_BLUE, 0, 1);
    ds(280, 230, "MIMARI: X86 (32 BIT)", CTXT, 0, 1);
    char buf[64]; kcpy(buf, "FIZIKSEL RAM: ");
    if(SYS_RAM_MB){ itoa((int)SYS_RAM_MB, buf + klen(buf)); kcpy(buf + klen(buf), " MB"); }
    else kcpy(buf + klen(buf), "BILINMIYOR");
    ds(280, 260, buf, CGN, 0, 1);
    ds(280, 300, "Depolama ve ag surucusu henuz yok.", CGY, 0, 1);
    char ub[48]; kcpy(ub, "KULLANICI: "); kcpy(ub + klen(ub), USER_NAME);
    ds(280, 340, ub, CTXT, 0, 1);
}

static void WINDAI_ASSISTANT(void){
    if(!AI_OPEN) return;
    i32 aw = 500, ah = 200;
    i32 ax = ((i32)SW - aw) / 2, ay = ((i32)SH - ah) / 2;
    DRAW_WINDOW(ax, ay, aw, ah, "WINDAI", BG_BASE);
    ds(ax + 20, ay + 80, "WINDAI henuz bagli degil.", CTXT, 0, 1);
    ds(ax + 20, ay + 110, "Yapay zeka modeli eklenmedi; bu pencere su an bos.", CGY, 0, 1);
}

/* ========================================================================= */
/* UYGULAMALAR: MUZIK, MIKU, NOT, YILAN, TERMINAL, TARAYICI, SPOTLIGHT       */
/* ========================================================================= */

/* PC hoparlorunu PIT kanal 2 ile calistirir (ses karti gerekmez) */
static void spk_set(u32 hz){
    if(hz == 0){ outb(0x61, inb(0x61) & 0xFC); return; }
    u32 div = 1193182u / hz;
    if(div == 0) div = 1;
    if(div > 0xFFFF) div = 0xFFFF;
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));
    u8 v = inb(0x61);
    if((v & 3) != 3) outb(0x61, (u8)(v | 3));
}

typedef struct { u16 hz; u8 frames; } Note;
#define NUM_TRACKS 2
#define TRK_LEN    8
static const Note MEL1[TRK_LEN] = {{523,20},{587,20},{659,20},{784,20},{659,20},{587,20},{523,40},{0,10}};
static const Note MEL2[TRK_LEN] = {{440,18},{494,18},{523,18},{659,36},{587,18},{523,18},{494,36},{0,8}};
static const Note *TRK_NOTES[NUM_TRACKS] = { MEL1, MEL2 };
static const char *TRK_NAME[NUM_TRACKS]  = { "MELODI 1", "MELODI 2" };
static int MUS_TRACK = 0, MUS_IDX = 0, MUS_LEFT = 0, MUS_PLAYING = 0;

static void mus_start(int t){ MUS_TRACK = t; MUS_IDX = 0; MUS_LEFT = 0; MUS_PLAYING = 1; }
static void mus_stop(void){ MUS_PLAYING = 0; MUS_IDX = 0; MUS_LEFT = 0; spk_set(0); }

/* Her kare bir adim: tempo kare sayisina baglidir */
static void mus_tick(void){
    if(!MUS_PLAYING) return;
    if(MUS_LEFT > 0){ MUS_LEFT--; return; }
    if(MUS_IDX >= TRK_LEN){ mus_stop(); return; }
    const Note *n = &TRK_NOTES[MUS_TRACK][MUS_IDX];
    spk_set(n->hz);
    MUS_LEFT = (int)n->frames - 1;
    MUS_IDX++;
}

static void MUSIC_APP(void){
    if(!MUS_OPEN) return;
    const i32 wx = 40, wy = 400, ww = 420, wh = 270;
    DRAW_WINDOW(wx, wy, ww, wh, "MUZIK CALAR", PAN_BG);
    if(CLK(wx + 14, wy + 11, 12, 12)){ MUS_OPEN = 0; mus_stop(); return; }

    ds(wx + 20, wy + 50, "PARCA:", CGY, 0, 1);
    ds(wx + 80, wy + 50, TRK_NAME[MUS_TRACK], CW, 0, 1);
    ds(wx + 20, wy + 68, MUS_PLAYING ? "DURUM: CALIYOR" : "DURUM: DURDU", MUS_PLAYING ? CHR_GRN : CGY, 0, 1);
    i32 prog = (MUS_IDX * 360) / TRK_LEN;
    rr(wx + 20, wy + 90, 360, 10, 4, PAN_BD);
    if(prog > 0) rr(wx + 20, wy + 90, prog, 10, 4, CHR_GRN);

    rr(wx + 20, wy + 115, 110, 32, 6, CHR_GRN);
    ds(wx + 36, wy + 127, MUS_PLAYING ? "DURAKLAT" : "CAL", CK, 0, 1);
    if(CLK(wx + 20, wy + 115, 110, 32)){ if(MUS_PLAYING) mus_stop(); else mus_start(MUS_TRACK); }

    rr(wx + 140, wy + 115, 110, 32, 6, CRD);
    ds(wx + 160, wy + 127, "DURDUR", CW, 0, 1);
    if(CLK(wx + 140, wy + 115, 110, 32)) mus_stop();

    rr(wx + 260, wy + 115, 140, 32, 6, WIN_BLUE);
    ds(wx + 272, wy + 127, "SONRAKI PARCA", CW, 0, 1);
    if(CLK(wx + 260, wy + 115, 140, 32)) mus_start((MUS_TRACK + 1) % NUM_TRACKS);

    ds(wx + 20, wy + 165, "CALMA LISTESI (tiklayin):", CGY, 0, 1);
    for(int t = 0; t < NUM_TRACKS; t++){
        i32 ty = wy + 185 + t * 20;
        rr(wx + 20, ty, 360, 16, 4, t == MUS_TRACK ? PAN_BD : PAN_BG);
        ds(wx + 30, ty + 4, TRK_NAME[t], t == MUS_TRACK ? CW : CTXT, 0, 1);
        if(CLK(wx + 20, ty, 360, 16)) mus_start(t);
    }
    ds(wx + 20, wy + wh - 22, "PC hoparlorunden kare dalga; ses karti yok.", CGY, 0, 1);
}

/* ---- MIKU ICERIK KARTI (yerlesik) ---- */
static int MIKU_MOOD = 0, MIKU_T = 0;
static void MIKU_APP(void){
    if(!MIKU_OPEN) return;
    const i32 wx = 420, wy = 60, ww = 340, wh = 330;
    const u32 TEAL = 0xFF39C5BBu;
    DRAW_WINDOW(wx, wy, ww, wh, "MIKU ICERIK KARTI", PAN_BG);
    if(CLK(wx + 14, wy + 11, 12, 12)){ MIKU_OPEN = 0; return; }

    rr(wx + 20, wy + 50, ww - 40, 190, 14, TEAL);
    i32 ph = MIKU_T % 40;
    i32 off = ph < 20 ? ph : 40 - ph;
    disc(wx + ww / 2, wy + 95 + off, 22, CW);
    disc(wx + ww / 2 - 8, wy + 91 + off, 3, CK);
    disc(wx + ww / 2 + 8, wy + 91 + off, 3, CK);
    const char *msg = MIKU_MOOD == 0 ? "Merhaba!" : (MIKU_MOOD == 1 ? "Muzik zamani!" : "Zzz...");
    dsc(wx + 20, wy + 195, ww - 40, msg, CK, 0, 1);
    if(CLK(wx + 20, wy + 50, ww - 40, 190)) MIKU_MOOD = (MIKU_MOOD + 1) % 3;

    ds(wx + 20, wy + 255, "Karta tikla: ruh hali degisir.", CGY, 0, 1);
    ds(wx + 20, wy + 268, "Dosya yok, yerlesik icerik.", CGY, 0, 1);
    rr(wx + 20, wy + 290, 170, 26, 6, WIN_BLUE);
    ds(wx + 30, wy + 299, "MELODI 1 CAL", CW, 0, 1);
    if(CLK(wx + 20, wy + 290, 170, 26)){ MUS_OPEN = 1; mus_start(0); MIKU_MOOD = 1; }
    MIKU_T++;
}

/* ---- NOT DEFTERI ---- */
static char NOTE_BUF[512];
static int  NOTE_LEN = 0;

static void note_key(u8 c){
    if(c == 8){ if(NOTE_LEN > 0) NOTE_LEN--; return; }
    if(c == '\r') c = '\n';
    if(c == '\n' || (c >= 32 && c < 127)){
        if(NOTE_LEN < (int)sizeof(NOTE_BUF) - 1) NOTE_BUF[NOTE_LEN++] = (char)c;
    }
}

static void NOTES_APP(void){
    if(!NOTE_OPEN) return;
    const i32 wx = 660, wy = 60, ww = 300, wh = 300;
    DRAW_WINDOW(wx, wy, ww, wh, "NOT DEFTERI", PAN_BG);
    if(CLK(wx + 14, wy + 11, 12, 12)){ NOTE_OPEN = 0; return; }
    if(MLB && !PMLB && HOV(wx, wy, ww, wh)) FOCUS = 2;

    rr(wx + 12, wy + 46, ww - 24, wh - 58, 8, SAFE_BLK);
    const i32 maxcol = (ww - 40) / 8;
    i32 x = wx + 20, y = wy + 54, col = 0;
    for(int i = 0; i < NOTE_LEN; i++){
        char ch = NOTE_BUF[i];
        if(ch == '\n' || col >= maxcol){ x = wx + 20; y += 12; col = 0; if(ch == '\n') continue; }
        if(y > wy + wh - 24) break;
        dc(x, y, ch, CTXT, 0, 1);
        x += 8; col++;
    }
    if(FOCUS == 2 && y <= wy + wh - 24) dc(x, y, '_', CGN, 0, 1);
}

/* ---- YILAN OYUNU ---- */
#define SN_W    24
#define SN_H    16
#define SN_CELL 12
static int SN_LEN = 0, SN_DIR = 1, SN_DEAD = 0, SN_SCORE = 0, SN_TICK = 0, SN_FX = 0, SN_FY = 0;
static int SN_X[SN_W * SN_H], SN_Y[SN_W * SN_H];
static u32 SN_RND = 12345u;

static int sn_rand(int n){ SN_RND = SN_RND * 1103515245u + 12345u; return (int)((SN_RND >> 16) % (u32)n); }
static int sn_on(int x, int y){ for(int i = 0; i < SN_LEN; i++) if(SN_X[i] == x && SN_Y[i] == y) return 1; return 0; }
static void sn_food(void){ int x, y; do { x = sn_rand(SN_W); y = sn_rand(SN_H); } while(sn_on(x, y)); SN_FX = x; SN_FY = y; }
static void sn_reset(void){
    SN_LEN = 3; SN_DIR = 1; SN_DEAD = 0; SN_SCORE = 0; SN_TICK = 0;
    for(int i = 0; i < SN_LEN; i++){ SN_X[i] = 6 - i; SN_Y[i] = 8; }
    sn_food();
}
static void snake_open(void){ SN_OPEN = 1; sn_reset(); FOCUS = 3; }

static void sn_key(u8 c){
    if(c == KEY_UP    && SN_DIR != 2) SN_DIR = 0;
    else if(c == KEY_RIGHT && SN_DIR != 3) SN_DIR = 1;
    else if(c == KEY_DOWN  && SN_DIR != 0) SN_DIR = 2;
    else if(c == KEY_LEFT  && SN_DIR != 1) SN_DIR = 3;
    else if(c == 'r' && SN_DEAD) sn_reset();
}

static void sn_step(void){
    if(SN_DEAD) return;
    int hx = SN_X[0], hy = SN_Y[0];
    if(SN_DIR == 0) hy--; else if(SN_DIR == 1) hx++; else if(SN_DIR == 2) hy++; else hx--;
    if(hx < 0 || hy < 0 || hx >= SN_W || hy >= SN_H){ SN_DEAD = 1; return; }
    if(sn_on(hx, hy)){ SN_DEAD = 1; return; }
    int ate = (hx == SN_FX && hy == SN_FY);
    if(ate && SN_LEN < SN_W * SN_H - 1){ SN_LEN++; SN_SCORE++; }
    for(int i = SN_LEN - 1; i > 0; i--){ SN_X[i] = SN_X[i - 1]; SN_Y[i] = SN_Y[i - 1]; }
    SN_X[0] = hx; SN_Y[0] = hy;
    if(ate) sn_food();
}

static void snake_tick(void){
    if(!SN_OPEN) return;
    if(++SN_TICK >= 6){ SN_TICK = 0; sn_step(); }
}

static void SNAKE_APP(void){
    if(!SN_OPEN) return;
    const i32 wx = 640, wy = 360, ww = 360, wh = 290;
    DRAW_WINDOW(wx, wy, ww, wh, "YILAN OYUNU", PAN_BG);
    if(CLK(wx + 14, wy + 11, 12, 12)){ SN_OPEN = 0; return; }
    if(MLB && !PMLB && HOV(wx, wy, ww, wh)) FOCUS = 3;

    const i32 gx = wx + 20, gy = wy + 46, cs = SN_CELL;
    fr(gx, gy, SN_W * cs, SN_H * cs, SAFE_BLK);
    fr(gx + SN_FX * cs + 2, gy + SN_FY * cs + 2, cs - 4, cs - 4, CRD);
    for(int i = 0; i < SN_LEN; i++)
        fr(gx + SN_X[i] * cs + 1, gy + SN_Y[i] * cs + 1, cs - 2, cs - 2, i == 0 ? CHR_GRN : CGN);

    char b[32]; kcpy(b, "SKOR: "); itoa(SN_SCORE, b + klen(b));
    ds(wx + 20, wy + 250, b, CTXT, 0, 1);
    ds(wx + 20, wy + 264, "Ok tuslari: yon. R: yeniden basla", CGY, 0, 1);
    if(SN_DEAD){
        ds(gx + 60, gy + 80, "OYUN BITTI", CRD, 0, 1);
        ds(gx + 24, gy + 100, "R ile yeniden basla", CW, 0, 1);
    }
}

static void tl_add(const char *s);

/* ---- PCI: yapilandirma alani (port 0xCF8 adres, 0xCFC veri) ---- */
static inline void outl(u16 p, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u32  inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

static u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off){
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 31) << 11)
             | ((u32)(fn & 7) << 8) | (u32)(off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

#define PCI_MAX 32
typedef struct { u16 vendor, device; u8 bus, dev, fn, cls, sub; u32 bar0; } PciDev;
static PciDev PCI_LIST[PCI_MAX];
static int PCI_N = 0;

/* Ag karti (RTL8139) bulunursa burada tutulur; bir sonraki adim kullanir */
#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139
static int NIC_FOUND = 0;
static PciDev NIC;

/* 0..7 fonksiyon basina: satici 0xFFFF ise cihaz yok */
static void pci_scan(void){
    PCI_N = 0; NIC_FOUND = 0;
    for(u8 d = 0; d < 32; d++){
        u32 id = pci_read32(0, d, 0, 0x00);
        if((id & 0xFFFF) == 0xFFFF) continue;
        /* Cok fonksiyonlu cihaz mi? (header tipi bit 7) */
        u32 hdr = pci_read32(0, d, 0, 0x0C);
        int multi = (hdr >> 23) & 1;
        int maxf = multi ? 8 : 1;
        for(u8 f = 0; f < maxf; f++){
            u32 fid = pci_read32(0, d, f, 0x00);
            if((fid & 0xFFFF) == 0xFFFF) continue;
            if(PCI_N >= PCI_MAX) return;
            u32 cls = pci_read32(0, d, f, 0x08);
            PciDev *p = &PCI_LIST[PCI_N++];
            p->vendor = (u16)(fid & 0xFFFF);
            p->device = (u16)(fid >> 16);
            p->bus = 0; p->dev = d; p->fn = f;
            p->cls = (u8)(cls >> 24);
            p->sub = (u8)(cls >> 16);
            p->bar0 = pci_read32(0, d, f, 0x10);
            if(p->vendor == RTL_VENDOR && p->device == RTL_DEVICE && !NIC_FOUND){
                NIC = *p; NIC_FOUND = 1;
            }
        }
    }
}

/* Sayiyi onaltilik metin olarak yazar: digits basamak, 0x oneki yok */
static void hex_str(u32 v, int digits, char *out){
    const char *h = "0123456789ABCDEF";
    for(int i = digits - 1; i >= 0; i--){ out[i] = h[v & 0xF]; v >>= 4; }
    out[digits] = 0;
}

/* lspci: bulunan PCI cihazlarini terminale satir satir yazar */
static void pci_list_to_term(void){
    pci_scan();
    char line[80];
    if(PCI_N == 0){ tl_add("PCI cihazi bulunamadi."); return; }
    for(int i = 0; i < PCI_N; i++){
        PciDev *p = &PCI_LIST[i];
        char vid[5], did[5], cl[3], sb[3];
        hex_str(p->vendor, 4, vid); hex_str(p->device, 4, did);
        hex_str(p->cls, 2, cl); hex_str(p->sub, 2, sb);
        char bus_dev[12];
        bus_dev[0]='0'; bus_dev[1]='0'; bus_dev[2]=':';
        hex_str(p->dev, 2, bus_dev + 3); bus_dev[5]='.'; bus_dev[6]='0'+p->fn; bus_dev[7]=0;
        /* 00:03.0 8086:100E  sinif 02:00 */
        int k = 0;
        for(int j = 0; bus_dev[j] && k < 79; j++) line[k++] = bus_dev[j];
        line[k++] = ' ';
        for(int j = 0; j < 4 && k < 79; j++) line[k++] = vid[j];
        line[k++] = ':';
        for(int j = 0; j < 4 && k < 79; j++) line[k++] = did[j];
        const char *mid = "  sinif ";
        for(int j = 0; mid[j] && k < 79; j++) line[k++] = mid[j];
        for(int j = 0; j < 2 && k < 79; j++) line[k++] = cl[j];
        line[k++] = ':';
        for(int j = 0; j < 2 && k < 79; j++) line[k++] = sb[j];
        line[k] = 0;
        tl_add(line);
    }
    if(NIC_FOUND){
        char b[80]; int k = 0;
        const char *m = "RTL8139 AG KARTI BULUNDU, I/O TABANI: 0x";
        for(int j = 0; m[j] && k < 70; j++) b[k++] = m[j];
        char io[5]; hex_str(NIC.bar0 & 0xFFFC, 4, io);
        for(int j = 0; j < 4; j++) b[k++] = io[j];
        b[k] = 0;
        tl_add(b);
    } else {
        tl_add("RTL8139 bulunamadi (QEMU -nic user,model=rtl8139 gerekli).");
    }
}

/* ---- TERMINAL ---- */
#define TL_MAX 24
#define TL_W   80
static char TLINES[TL_MAX][TL_W];
static int  TL_N = 0;
static char TIN[TL_W];
static int  TIN_LEN = 0;

/* Gecmise satir ekler; dolunca en eski satiri atar */
static void tl_add(const char *s){
    int slot;
    if(TL_N < TL_MAX){ slot = TL_N++; }
    else {
        for(int i = 1; i < TL_MAX; i++) for(int j = 0; j < TL_W; j++) TLINES[i - 1][j] = TLINES[i][j];
        slot = TL_MAX - 1;
    }
    int i = 0;
    while(s[i] && i < TL_W - 1){ TLINES[slot][i] = s[i]; i++; }
    TLINES[slot][i] = 0;
}

static void app_str(char *dst, const char *a, const char *b){
    int k = 0;
    while(a[k] && k < TL_W - 1){ dst[k] = a[k]; k++; }
    int j = 0;
    while(b[j] && k < TL_W - 1){ dst[k++] = b[j++]; }
    dst[k] = 0;
}

static int str_eq(const char *a, const char *b){ while(*a && *a == *b){ a++; b++; } return *a == *b; }

/* ========================================================================= */
/* AG: RTL8139 SURUCUSU (yoklamali), ARP, IPv4, ICMP (ping)                  */
/* ========================================================================= */
/* Kart DMA ile RX_BUF'a yazar; her karede nic_poll() ile okunur.
   GRUB altinda sayfalama kapali: fiziksel adres = sanal adres.            */

static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }

static void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 v){
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 31) << 11)
             | ((u32)(fn & 7) << 8) | (u32)(off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, v);
}

#define RX_RING  8192
#define RX_BUFSZ (RX_RING + 16 + 1536)
static u8  RX_BUF[RX_BUFSZ] __attribute__((aligned(16)));
static u8  TX_BUF[4][1536] __attribute__((aligned(16)));
static u16 NIC_IO = 0;
static u8  NIC_MAC[6];
static int NIC_UP = 0;
static u32 RX_OFF = 0;
static u8  TX_IDX = 0;
static u16 IP_ID = 1;

/* QEMU kullanici agi (10.0.2.0/24, ag gecidi 10.0.2.2) */
#define NET_IP  0x0A00020Fu
#define NET_GW  0x0A000202u
#define PING_ID 0x5744

#define ARP_N 8
static u32 ARP_IP[ARP_N];
static u8  ARP_MAC[ARP_N][6];
static int ARP_CNT = 0;

static u16 PING_SEQ = 0;
static u32 PING_T1 = 0;
static int PING_GOT = 0;

/* Ag bayti: buyuk endian (ag) ve kart bayti: kucuk endian (RX basligi) */
static void put16(u8 *p, u16 v){ p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u16 get16(const u8 *p){ return (u16)((p[0] << 8) | p[1]); }
static void put32(u8 *p, u32 v){ p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 get32(const u8 *p){ return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

static u16 inet_csum(const u8 *d, int n){
    u32 s = 0;
    for(int i = 0; i + 1 < n; i += 2) s += (u32)((d[i] << 8) | d[i + 1]);
    if(n & 1) s += (u32)(d[n - 1] << 8);
    while(s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

static int nic_send(const u8 *pkt, u16 len){
    if(!NIC_UP || len < 14 || len > 1536) return 0;
    u8 i = TX_IDX;
    TX_IDX = (u8)((TX_IDX + 1) & 3);
    for(u16 k = 0; k < len; k++) TX_BUF[i][k] = pkt[k];
    outl(NIC_IO + 0x20 + i * 4, (u32)(unsigned long)TX_BUF[i]);   /* TSAD */
    outl(NIC_IO + 0x10 + i * 4, len);                             /* TSD: boyut, gonderimi baslatir */
    for(u32 t = 0; t < 400000; t++){
        if(inl(NIC_IO + 0x10 + i * 4) & 0x8000) return 1;         /* TOK */
    }
    return 0;
}

static int eth_send(const u8 *dst, u16 type, const u8 *body, int n){
    u8 f[1536];
    if(n < 0 || n > 1500) return 0;
    for(int i = 0; i < 6; i++){ f[i] = dst[i]; f[6 + i] = NIC_MAC[i]; }
    put16(f + 12, type);
    for(int i = 0; i < n; i++) f[14 + i] = body[i];
    int total = 14 + n;
    while(total < 60) f[total++] = 0;                             /* minimum kare boyu */
    return nic_send(f, (u16)total);
}

static void arp_put(u32 ip, const u8 *mac){
    int i = 0;
    while(i < ARP_CNT && ARP_IP[i] != ip) i++;
    if(i == ARP_CNT){ if(ARP_CNT >= ARP_N) return; ARP_IP[i] = ip; ARP_CNT++; }
    for(int k = 0; k < 6; k++) ARP_MAC[i][k] = mac[k];
}

static int arp_get(u32 ip, u8 *mac){
    for(int i = 0; i < ARP_CNT; i++){
        if(ARP_IP[i] == ip){ for(int k = 0; k < 6; k++) mac[k] = ARP_MAC[i][k]; return 1; }
    }
    return 0;
}

static void arp_send(u16 oper, const u8 *tha, u32 tpa, const u8 *dst){
    u8 a[28];
    put16(a, 1); put16(a + 2, 0x0800); a[4] = 6; a[5] = 4; put16(a + 6, oper);
    for(int i = 0; i < 6; i++) a[8 + i] = NIC_MAC[i];
    put32(a + 14, NET_IP);
    for(int i = 0; i < 6; i++) a[18 + i] = tha[i];
    put32(a + 24, tpa);
    eth_send(dst, 0x0806, a, 28);
}

static void arp_request(u32 ip){
    static const u8 bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const u8 zero[6] = {0, 0, 0, 0, 0, 0};
    arp_send(1, zero, ip, bc);
}

static void arp_rx(const u8 *a, int n){
    if(n < 28) return;
    u16 oper = get16(a + 6);
    u32 spa = get32(a + 14), tpa = get32(a + 24);
    arp_put(spa, a + 8);
    if(oper == 1 && tpa == NET_IP) arp_send(2, a + 8, spa, a + 8);
}

static int ip_send(u32 dst, u8 proto, const u8 *body, int n){
    u8 p[1500], mac[6];
    if(n < 0 || n > 1480) return 0;
    u32 nh = (((dst ^ NET_IP) & 0xFFFFFF00u) == 0) ? dst : NET_GW;
    if(!arp_get(nh, mac)) return 0;
    p[0] = 0x45; p[1] = 0;
    put16(p + 2, (u16)(20 + n));
    put16(p + 4, IP_ID++);
    put16(p + 6, 0x4000);
    p[8] = 64; p[9] = proto;
    put16(p + 10, 0);
    put32(p + 12, NET_IP);
    put32(p + 16, dst);
    put16(p + 10, inet_csum(p, 20));
    for(int i = 0; i < n; i++) p[20 + i] = body[i];
    return eth_send(mac, 0x0800, p, 20 + n);
}

static void icmp_rx(u32 src, const u8 *m, int n){
    if(n < 8) return;
    if(m[0] == 8){                                 /* yanki istegi: yanitla */
        u8 r[1480];
        if(n > 1480) return;
        for(int i = 0; i < n; i++) r[i] = m[i];
        r[0] = 0; r[2] = 0; r[3] = 0;
        put16(r + 2, inet_csum(r, n));
        ip_send(src, 1, r, n);
    } else if(m[0] == 0 && get16(m + 4) == PING_ID){
        PING_GOT = 1; PING_T1 = TICKS;
    }
}

static void tcp_rx(u32 src, const u8 *s, int n);

/* ---- GUVENLIK DUVARI (basit paket filtresi) ----
   Gelen IPv4 paketleri islenmeden once denetlenir:
   - engelli IP listesindeki kaynaklardan gelen tum paketler dusurulur
   - duvar aciksa, izinli port disindaki yeni TCP baglanti istekleri (SYN) dusurulur
   Giden trafige dokunmaz. ICMP (ping) varsayilan olarak izinlidir. */
#define FW_BLK_MAX   8
#define FW_ALLOW_MAX 8
static int FW_ON = 1;
static u32 FW_BLK[FW_BLK_MAX];
static int FW_BLK_N = 0;
static u32 FW_ALLOW[FW_ALLOW_MAX];   /* SYN'e izin verilen TCP portlari */
static int FW_ALLOW_N = 0;
static u32 FW_DROP = 0;              /* dusurulen paket sayisi */

/* 1: paket dusurulmeli. s: TCP segmenti (proto 6) ya da ICMP govdesi */
static int fw_drop(u32 src, u8 proto, const u8 *s, int n){
    if(!FW_ON) return 0;
    for(int i = 0; i < FW_BLK_N; i++) if(FW_BLK[i] == src){ FW_DROP++; return 1; }
    if(proto == 6 && n >= 14 && (s[13] & 0x12) == 0x02){     /* SYN var, ACK yok: yeni baglanti */
        u32 port = get16(s + 2);
        int ok = 0;
        for(int i = 0; i < FW_ALLOW_N; i++) if(FW_ALLOW[i] == port) ok = 1;
        if(!ok){ FW_DROP++; return 1; }
    }
    return 0;
}

static void ip_rx(const u8 *p, int n){
    if(n < 20 || (p[0] >> 4) != 4) return;
    int ihl = (p[0] & 0x0F) * 4;
    int tot = get16(p + 2);
    if(ihl < 20 || tot < ihl || tot > n) return;
    if(get32(p + 16) != NET_IP) return;
    if(fw_drop(get32(p + 12), p[9], p + ihl, tot - ihl)) return;
    if(p[9] == 1) icmp_rx(get32(p + 12), p + ihl, tot - ihl);
    else if(p[9] == 6) tcp_rx(get32(p + 12), p + ihl, tot - ihl);
}

static void net_rx(const u8 *f, int n){
    if(n < 14) return;
    u16 type = get16(f + 12);
    if(type == 0x0806) arp_rx(f + 14, n - 14);
    else if(type == 0x0800) ip_rx(f + 14, n - 14);
}

static void nic_rx_reset(void){
    outb(NIC_IO + 0x37, 0x00);
    outl(NIC_IO + 0x30, (u32)(unsigned long)RX_BUF);
    outw(NIC_IO + 0x38, 0xFFF0);
    outb(NIC_IO + 0x37, 0x0C);
    RX_OFF = 0;
}

/* Bekleyen kareleri alip isler (her kare cagrilir) */
static void nic_poll(void){
    if(!NIC_UP) return;
    for(int k = 0; k < 8 && !(inb(NIC_IO + 0x37) & 0x01); k++){   /* BUFE=0: kare var */
        const u8 *h = RX_BUF + RX_OFF;
        u16 status = (u16)(h[0] | (h[1] << 8));
        u16 len    = (u16)(h[2] | (h[3] << 8));
        if(!(status & 0x0001) || len < 4 || len > 1536){ nic_rx_reset(); return; }
        net_rx(h + 4, len - 4);                                   /* CRC cikarilir */
        RX_OFF = (RX_OFF + len + 4 + 3) & ~3u;
        RX_OFF &= (RX_RING - 1);
        outw(NIC_IO + 0x38, (u16)(RX_OFF - 16));                  /* CAPR */
    }
    outw(NIC_IO + 0x3E, 0xFFFF);                                  /* ISR temizle */
}

/* ARP cozumleme: en fazla ~1.5 sn bekler (komut sirasinda kullanilir) */
static int arp_resolve(u32 ip){
    u8 m[6];
    for(int tries = 0; tries < 3; tries++){
        if(arp_get(ip, m)) return 1;
        arp_request(ip);
        u32 until = TICKS + 25;
        while(TICKS < until && !arp_get(ip, m)){ nic_poll(); __asm__ volatile("hlt"); }
    }
    return arp_get(ip, m);
}

static int parse_ip(const char *s, u32 *out){
    u32 v = 0; int parts = 0;
    while(1){
        u32 n = 0; int d = 0;
        while(*s >= '0' && *s <= '9'){ n = n * 10 + (u32)(*s - '0'); if(n > 255) return 0; s++; d++; }
        if(!d) return 0;
        v = (v << 8) | n; parts++;
        if(*s == '.'){ s++; continue; }
        break;
    }
    while(*s == ' ') s++;
    if(*s || parts != 4) return 0;
    *out = v;
    return 1;
}

static void ip_str(u32 ip, char *o){
    int k = 0;
    for(int sh = 24; sh >= 0; sh -= 8){
        u32 v = (ip >> sh) & 0xFF;
        char t[3]; int n = 0;
        do { t[n++] = (char)('0' + v % 10); v /= 10; } while(v);
        while(n) o[k++] = t[--n];
        if(sh) o[k++] = '.';
    }
    o[k] = 0;
}

static void dec_str(u32 v, char *o){
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while(v);
    int k = 0;
    while(n) o[k++] = t[--n];
    o[k] = 0;
}

/* ping [ip]: varsayilan ag gecidi; sonuc terminale yazilir */
static void ping_cmd(const char *arg){
    u32 ip = NET_GW;
    if(!NIC_UP){ tl_add("AG KARTI YOK: QEMU -nic user,model=rtl8139 ile calistirin."); return; }
    if(*arg && !parse_ip(arg, &ip)){ tl_add("GECERSIZ IP ADRESI (ornek: ping 10.0.2.2)"); return; }
    int local = ((ip ^ NET_IP) & 0xFFFFFF00u) == 0;
    char ipst[16];
    ip_str(ip, ipst);
    if(!arp_resolve(local ? ip : NET_GW)){ tl_add("ARP YANITI ALINAMADI."); return; }
    u8 m[40];
    PING_SEQ++;
    m[0] = 8; m[1] = 0; m[2] = 0; m[3] = 0;
    put16(m + 4, PING_ID); put16(m + 6, PING_SEQ);
    for(int i = 8; i < 40; i++) m[i] = (u8)('a' + (i & 15));
    put16(m + 2, inet_csum(m, 40));
    PING_GOT = 0;
    u32 t0 = TICKS;
    if(!ip_send(ip, 1, m, 40)){ tl_add("PAKET GONDERILEMEDI."); return; }
    while(!PING_GOT && TICKS - t0 < 75){ nic_poll(); __asm__ volatile("hlt"); }
    char line[80], ms[12];
    if(PING_GOT){
        kcpy(line, ipst);
        kcpy(line + klen(line), " YANIT, SURE: ");
        if(PING_T1 == t0){                          /* kare cozunurlugu 20 ms: ayni karede geldi */
            kcpy(line + klen(line), "20 MS'DEN AZ");
        } else {
            dec_str((PING_T1 - t0) * 1000u / FRAME_HZ, ms);
            kcpy(line + klen(line), ms);
            kcpy(line + klen(line), " MS");
        }
    } else {
        kcpy(line, ipst);
        kcpy(line + klen(line), " YANIT VERMEDI (ZAMAN ASIMI)");
    }
    tl_add(line);
}

static void nic_init(void){
    pci_scan();
    if(!NIC_FOUND) return;
    u32 cmd = pci_read32(0, NIC.dev, NIC.fn, 0x04);
    pci_write32(0, NIC.dev, NIC.fn, 0x04, (cmd & 0xFFFFu) | 0x5u);   /* I/O + bus master */
    NIC_IO = (u16)(NIC.bar0 & 0xFFFCu);
    if(!NIC_IO) return;
    outb(NIC_IO + 0x52, 0x00);                                    /* CONFIG1: guc acik */
    outb(NIC_IO + 0x37, 0x10);                                    /* RST */
    for(u32 i = 0; i < 1000000 && (inb(NIC_IO + 0x37) & 0x10); i++);
    for(int i = 0; i < 6; i++) NIC_MAC[i] = inb(NIC_IO + i);
    outl(NIC_IO + 0x30, (u32)(unsigned long)RX_BUF);              /* RBSTART */
    outw(NIC_IO + 0x38, 0xFFF0);                                  /* CAPR */
    outw(NIC_IO + 0x3C, 0x0000);                                  /* IMR: kesme yok, yoklama */
    outw(NIC_IO + 0x3E, 0xFFFF);
    outl(NIC_IO + 0x44, 0x0000E78Fu);                             /* RCR: yayin+coklu+hepsi+WRAP */
    outl(NIC_IO + 0x40, 0x03000700u);                             /* TCR */
    outb(NIC_IO + 0x37, 0x0C);                                    /* TE | RE */
    NIC_UP = 1;
}

/* ========================================================================= */
/* SES: INTEL AC97 SURUCUSU (PCM cikisi, DMA tamponlari)                     */
/* ========================================================================= */
/* PCI sinif 04:01 (cokluortam ses). NAM = mikser (BAR0), NABM = DMA (BAR1).
   Cikis: 16 bit stereo, 48000 Hz. 32 tampon x 2048 kare = ~1.37 sn.        */

#define AC97_FRAMES 2048
#define AC97_BDL_N  32
typedef struct { u32 addr; u16 len; u16 ctl; } AcBdl;
static AcBdl AC_BDL[AC97_BDL_N] __attribute__((aligned(8)));
static short AC_PCM[AC97_BDL_N][AC97_FRAMES * 2] __attribute__((aligned(16)));
static u16 AC_NAM = 0, AC_NABM = 0;
static int AC_UP = 0;
static int AC_PLAYING = 0;
static u32 AC_STOP_T = 0;

static void ac97_init(void){
    PciDev *d = 0;
    for(int i = 0; i < PCI_N; i++){
        if(PCI_LIST[i].cls == 0x04 && PCI_LIST[i].sub == 0x01){ d = &PCI_LIST[i]; break; }
    }
    if(!d) return;
    u32 cmd = pci_read32(0, d->dev, d->fn, 0x04);
    pci_write32(0, d->dev, d->fn, 0x04, (cmd & 0xFFFFu) | 0x5u);   /* I/O + bus master */
    AC_NAM  = (u16)(d->bar0 & 0xFFFCu);
    AC_NABM = (u16)(pci_read32(0, d->dev, d->fn, 0x14) & 0xFFC0u);
    if(!AC_NAM || !AC_NABM) return;
    outl(AC_NABM + 0x2C, 0x00000002u);                 /* GC: soguk reset cikisi */
    for(u32 i = 0; i < 500000 && !(inl(AC_NABM + 0x30) & 0x100); i++);  /* GS: codec hazir */
    outw(AC_NAM + 0x00, 0x0000);                       /* codec reset */
    outw(AC_NAM + 0x02, 0x0808);                       /* ana ses (sessiz degil, orta) */
    outw(AC_NAM + 0x18, 0x0808);                       /* PCM cikis sesi */
    outw(AC_NAM + 0x2C, 48000);                        /* PCM ornekleme hizi */
    AC_UP = 1;
}

/* Kare dizisi: kare kare kare dalgasi (genlik 6000), faz tamponlar arasinda surer */
static void ac97_fill(u32 hz){
    u32 period = 48000u / hz;
    if(period < 2) period = 2;
    static u32 ph = 0;
    for(int b = 0; b < AC97_BDL_N; b++){
        for(int f = 0; f < AC97_FRAMES; f++){
            short v = (ph < period / 2) ? 6000 : -6000;
            AC_PCM[b][f * 2]     = v;
            AC_PCM[b][f * 2 + 1] = v;
            ph = (ph + 1 >= period) ? 0 : ph + 1;
        }
        AC_BDL[b].addr = (u32)(unsigned long)AC_PCM[b];
        AC_BDL[b].len  = AC97_FRAMES * 2;              /* ornek (16 bit) sayisi */
        AC_BDL[b].ctl  = 0x8000;                       /* IOC: tampon bitince kesme isareti */
    }
}

static void ac97_stop(void){
    if(!AC_UP) return;
    outb(AC_NABM + 0x1B, 0x00);                        /* PO CR: durdur */
    AC_PLAYING = 0;
}

static void ac97_play(u32 hz, u32 ms){
    if(!AC_UP){ tl_add("SES KARTI YOK: QEMU -device AC97 ile calistirin."); return; }
    ac97_fill(hz);
    outb(AC_NABM + 0x1B, 0x02);                        /* PO reset */
    for(int i = 0; i < 100000 && (inb(AC_NABM + 0x1B) & 0x02); i++);
    outl(AC_NABM + 0x10, (u32)(unsigned long)AC_BDL);  /* PO BDBAR */
    outb(AC_NABM + 0x15, AC97_BDL_N - 1);              /* LVI: son tampon */
    outw(AC_NABM + 0x16, 0x001C);                      /* SR: bayraklari temizle */
    outb(AC_NABM + 0x1B, 0x01);                        /* RPBM: calistir */
    AC_PLAYING = 1;
    u32 ticks = ms * FRAME_HZ / 1000;
    AC_STOP_T = TICKS + (ticks ? ticks : 1);
}

/* Kare dongusunde cagrilir: sure dolunca ya da veri bitince durdurur */
static void ac97_tick(void){
    if(!AC_PLAYING) return;
    if(TICKS >= AC_STOP_T || (inb(AC_NABM + 0x1B) & 0x01) == 0) ac97_stop();
}

static u32 parse_uint(const char *s){
    u32 v = 0; int d = 0;
    while(*s >= '0' && *s <= '9' && d < 6){ v = v * 10 + (u32)(*s - '0'); s++; d++; }
    return d ? v : 0;
}

/* ses [hz]: 40..4000 Hz arasi kare dalga, 1 sn */
static void ses_cmd(const char *arg){
    u32 hz = *arg ? parse_uint(arg) : 440;
    if(hz < 40 || hz > 4000){ tl_add("GECERSIZ FREKANS (40-4000 HZ)."); return; }
    if(!AC_UP){ tl_add("SES KARTI YOK: QEMU -device AC97 ile calistirin."); return; }
    ac97_play(hz, 1000);
    char line[80], hs[12];
    dec_str(hz, hs);
    kcpy(line, "SES: ");
    kcpy(line + klen(line), hs);
    kcpy(line + klen(line), " HZ, 1 SN CALISIYOR");
    tl_add(line);
}

/* ========================================================================= */
/* TCP ISTEMCISI (tek baglanti) ve HTTP GET                                  */
/* ========================================================================= */
#define TF_FIN 0x01
#define TF_SYN 0x02
#define TF_RST 0x04
#define TF_PSH 0x08
#define TF_ACK 0x10
#define TCP_MSS 1400
#define TCP_RX_CAP 1048576   /* 1 MB: Chromium koprusunun BMP ekran goruntusu sigsin */
#define TCP_WIN 4096         /* alici penceresi; RTL8139 halkasi (8 KB) tasmasin diye kucuk */

enum { TS_CLOSED = 0, TS_SYN_SENT, TS_ESTAB, TS_DONE };
static int TCP_STATE = TS_CLOSED;
static u32 TCP_RIP = 0;
static u16 TCP_RPORT = 0, TCP_LPORT = 0;
static u32 TCP_SEQ = 0, TCP_ACK = 0;
static u8  TCP_RX[TCP_RX_CAP];
static int TCP_RXN = 0;
static int TCP_FIN = 0;
static u16 TCP_EPHEM = 49152;

static u16 tcp_csum(u32 src, u32 dst, const u8 *seg, int n){
    u8 ph[12];
    put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = 6; put16(ph + 10, (u16)n);
    u32 s = 0;
    for(int i = 0; i < 12; i += 2) s += (u32)((ph[i] << 8) | ph[i + 1]);
    for(int i = 0; i + 1 < n; i += 2) s += (u32)((seg[i] << 8) | seg[i + 1]);
    if(n & 1) s += (u32)(seg[n - 1] << 8);
    while(s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

/* Bir TCP segmenti gonderir; SYN/FIN sirayi bir artirir */
static int tcp_out(u8 flags, const u8 *data, int n){
    u8 seg[20 + TCP_MSS];
    if(n < 0 || n > TCP_MSS) return 0;
    put16(seg, TCP_LPORT); put16(seg + 2, TCP_RPORT);
    put32(seg + 4, TCP_SEQ); put32(seg + 8, TCP_ACK);
    seg[12] = 0x50; seg[13] = flags;
    put16(seg + 14, TCP_WIN); put16(seg + 16, 0); put16(seg + 18, 0);
    for(int i = 0; i < n; i++) seg[20 + i] = data[i];
    put16(seg + 16, tcp_csum(NET_IP, TCP_RIP, seg, 20 + n));
    if(!ip_send(TCP_RIP, 6, seg, 20 + n)) return 0;
    TCP_SEQ += (u32)n + ((flags & (TF_SYN | TF_FIN)) ? 1u : 0u);
    return 1;
}

static void tcp_rx(u32 src, const u8 *s, int n){
    if(n < 20) return;
    u16 sp = get16(s), dp = get16(s + 2);
    u32 seq = get32(s + 4), ack = get32(s + 8);
    int doff = (s[12] >> 4) * 4;
    u8 fl = s[13];
    if(doff < 20 || doff > n) return;
    if(src != TCP_RIP || sp != TCP_RPORT || dp != TCP_LPORT) return;
    if(fl & TF_RST){ TCP_STATE = TS_DONE; TCP_FIN = 1; return; }
    if(TCP_STATE == TS_SYN_SENT){
        if((fl & (TF_SYN | TF_ACK)) == (TF_SYN | TF_ACK) && ack == TCP_SEQ){
            TCP_ACK = seq + 1;
            TCP_STATE = TS_ESTAB;
            tcp_out(TF_ACK, 0, 0);
        }
        return;
    }
    if(TCP_STATE != TS_ESTAB) return;
    int pl = n - doff;
    if(pl > 0 && seq == TCP_ACK){                     /* sirali veri: al */
        for(int i = 0; i < pl && TCP_RXN < TCP_RX_CAP; i++) TCP_RX[TCP_RXN++] = s[doff + i];
        TCP_ACK += (u32)pl;
    }
    if((fl & TF_FIN) && seq + (u32)pl == TCP_ACK){    /* karsi taraf kapatiyor */
        TCP_ACK++;
        TCP_FIN = 1;
    }
    if(pl > 0 || (fl & TF_FIN)) tcp_out(TF_ACK, 0, 0);
}

/* SYN gonderir, SYN-ACK bekler (3 deneme, her biri ~1 sn) */
static int tcp_connect(u32 ip, u16 port){
    u32 nh = (((ip ^ NET_IP) & 0xFFFFFF00u) == 0) ? ip : NET_GW;
    if(!arp_resolve(nh)) return 0;
    TCP_RIP = ip; TCP_RPORT = port;
    TCP_LPORT = TCP_EPHEM++;
    if(TCP_EPHEM < 49152) TCP_EPHEM = 49152;
    TCP_RXN = 0; TCP_FIN = 0;
    u32 isn = 0x1000u + TICKS * 7919u;
    for(int tries = 0; tries < 3; tries++){
        TCP_STATE = TS_SYN_SENT;
        TCP_SEQ = isn; TCP_ACK = 0;
        tcp_out(TF_SYN, 0, 0);
        u32 until = TICKS + 50;
        while(TICKS < until && TCP_STATE == TS_SYN_SENT){ nic_poll(); __asm__ volatile("hlt"); }
        if(TCP_STATE != TS_SYN_SENT) break;
    }
    return TCP_STATE == TS_ESTAB;
}

/* HTTP/1.0 GET: yaniti TCP_RX'e doldurur (sunucu kapatinca biter, en fazla wait_ticks tick) */
static int http_get(u32 ip, u16 port, const char *path, const char *host, u32 wait_ticks){
    if(!tcp_connect(ip, port)) return 0;
    char req[512];
    int k = 0;
    const char *parts[5] = { "GET ", path, " HTTP/1.0\r\nHost: ", host, "\r\nConnection: close\r\n\r\n" };
    for(int i = 0; i < 5; i++){
        for(int j = 0; parts[i][j] && k < 500; j++) req[k++] = parts[i][j];
    }
    for(int off = 0; off < k; off += TCP_MSS){
        int c = (k - off < TCP_MSS) ? (k - off) : TCP_MSS;
        tcp_out(TF_PSH | TF_ACK, (const u8 *)req + off, c);
    }
    u32 until = TICKS + wait_ticks;
    while(!TCP_FIN && TICKS < until) nic_poll();   /* hlt yok: RTL8139 halkasi hizli bosalsin */
    if(TCP_FIN && TCP_STATE == TS_ESTAB) tcp_out(TF_FIN | TF_ACK, 0, 0);
    TCP_STATE = TS_DONE;
    return TCP_RXN > 0;
}

/* Yaniti terminale yazar: durum satiri, boyut ve govdenin ilk satirlari */
static void http_show(void){
    int hdr_end = -1;
    for(int i = 0; i + 3 < TCP_RXN; i++){
        if(TCP_RX[i] == '\r' && TCP_RX[i + 1] == '\n' && TCP_RX[i + 2] == '\r' && TCP_RX[i + 3] == '\n'){ hdr_end = i + 4; break; }
    }
    char line[80];
    int n = 0;
    for(int i = 0; i < TCP_RXN && i < 70 && TCP_RX[i] != '\r' && TCP_RX[i] != '\n'; i++) line[n++] = (char)TCP_RX[i];
    line[n] = 0;
    tl_add(line);
    char bs[12];
    dec_str((u32)TCP_RXN, bs);
    kcpy(line, "BOYUT: "); kcpy(line + klen(line), bs); kcpy(line + klen(line), " BAYT");
    tl_add(line);
    int b = (hdr_end < 0) ? 0 : hdr_end;
    int shown = 0;
    while(b < TCP_RXN && shown < 12){
        n = 0;
        while(b < TCP_RXN && TCP_RX[b] != '\n' && n < 76){
            if(TCP_RX[b] != '\r') line[n++] = (char)TCP_RX[b];
            b++;
        }
        while(b < TCP_RXN && TCP_RX[b] != '\n') b++;   /* uzun satirin kalani */
        b++;
        line[n] = 0;
        tl_add(line);
        shown++;
    }
}

/* URL: [http://]ip[:port][/yol] -> ip, port, yol. Ornek: 10.0.2.2:8080/index.html */
static int url_parse(const char *arg, u32 *ip, u16 *port, char *path){
    if(arg[0] == 'h' && arg[1] == 't' && arg[2] == 't' && arg[3] == 'p' && arg[4] == ':' && arg[5] == '/' && arg[6] == '/')
        arg += 7;
    char host[32]; int hn = 0;
    while(arg[hn] && arg[hn] != ':' && arg[hn] != '/' && arg[hn] != ' ' && hn < 31){ host[hn] = arg[hn]; hn++; }
    host[hn] = 0;
    if(!parse_ip(host, ip)) return 0;
    const char *p = arg + hn;
    u32 pt = 80;
    if(*p == ':'){ p++; pt = parse_uint(p); while(*p >= '0' && *p <= '9') p++; }
    if(pt == 0 || pt > 65535) return 0;
    *port = (u16)pt;
    path[0] = '/'; path[1] = 0;
    if(*p == '/'){
        int k = 0;
        while(p[k] && p[k] != ' ' && k < 94){ path[k] = p[k]; k++; }
        path[k] = 0;
    }
    return 1;
}

/* http ip[:port][/yol]: ornek http 10.0.2.2:8080/index.html */
static void http_cmd(const char *arg){
    if(!NIC_UP){ tl_add("AG KARTI YOK: QEMU -nic user,model=rtl8139 ile calistirin."); return; }
    u32 ip = 0; u16 port = 80; char path[96];
    if(!url_parse(arg, &ip, &port, path)){ tl_add("KULLANIM: http ip[:port][/yol]  (ornek: http 10.0.2.2:8080/index.html)"); return; }
    char ipst[16];
    ip_str(ip, ipst);
    if(!http_get(ip, port, path, ipst, 150)){ tl_add("BAGLANTI KURULAMADI (ZAMAN ASIMI)."); return; }
    http_show();
}

/* ---- .wind calistirici: ATA diskinden FAT ile dosya okur, programi belleğe yukler,
   kernel API tablosunu verip giris noktasini cagirir ---- */
/* Depolama: MVOL yazilabilir (ATA diski, yoksa RAM disk); SVOL salt okunur kaynak
   (GRUB'un yukledigi disk imaji). MVOL RAM ise ayni bellek oldugu icin SVOL kullanilmaz. */
static fat_vol MVOL, SVOL;
static int MVOL_STATE = 0, SVOL_STATE = 0;   /* 0: denenmedi, 1: hazir, -1: olmadi */
static int MVOL_RAM = 0;                     /* 1: MVOL RAM disk (yeniden baslatinca silinir) */
static unsigned char WIN_FILE[WIND_MAX_MEM + WIND_HDR_SIZE];
static unsigned char WIN_MEM[WIND_MAX_MEM];

/* RAM disk: GRUB'un yukledigi disk imaji (ISO'nun icinde, boot/disk.img).
   Varsa FAT birimi buradan okunur; yoksa ATA diski kullanilir. */
static const unsigned char *RD_BASE = 0;
static unsigned RD_LEN = 0;

static int ram_read(void *ctx, unsigned lba, unsigned count, unsigned char *buf){
    (void)ctx;
    if(!RD_BASE || lba > RD_LEN / 512) return -1;
    unsigned off = lba * 512;
    if(count > (RD_LEN - off) / 512) return -1;
    for(unsigned i = 0; i < count * 512; i++) buf[i] = RD_BASE[off + i];
    return 0;
}

static int ram_write(void *ctx, unsigned lba, unsigned count, const unsigned char *buf){
    (void)ctx;
    if(!RD_BASE || lba > RD_LEN / 512) return -1;
    unsigned off = lba * 512;
    if(count > (RD_LEN - off) / 512) return -1;
    unsigned char *dst = (unsigned char *)RD_BASE;   /* GRUB modulu: yazilabilir RAM */
    for(unsigned i = 0; i < count * 512; i++) dst[off + i] = buf[i];
    return 0;
}

/* MVOL: once ATA diski (kalici), yoksa RAM disk. Bir kez denenir, sonuc onbellekte */
static int mvol_ready(void){
    if(MVOL_STATE != 0) return MVOL_STATE > 0;
    MVOL_STATE = -1;
    if(fat_mount(&MVOL, ata_read, 0) == 0){
        fat_set_writer(&MVOL, ata_write);
        MVOL_RAM = 0; MVOL_STATE = 1;
    } else if(RD_BASE && fat_mount(&MVOL, ram_read, 0) == 0){
        fat_set_writer(&MVOL, ram_write);
        MVOL_RAM = 1; MVOL_STATE = 1;
    }
    return MVOL_STATE > 0;
}

/* SVOL: GRUB modulu (salt okunur). MVOL zaten RAM ise gerekmez */
static int svol_ready(void){
    if(SVOL_STATE != 0) return SVOL_STATE > 0;
    SVOL_STATE = -1;
    if(RD_BASE && fat_mount(&SVOL, ram_read, 0) == 0) SVOL_STATE = 1;
    return SVOL_STATE > 0;
}

#define PNAME_MAX 96   /* program adi tamponu (terminal satiri TL_W ile sinirli) */

static int has_dot(const char *s){
    for(; *s; s++) if(*s == '.') return 1;
    return 0;
}

/* dir + name yolunu dst'ye yazar; sigmazsa 0 */
static int path_join(char *dst, unsigned cap, const char *dir, const char *name){
    unsigned k = 0, j = 0;
    while(dir[k]){ if(k + 1 >= cap) return 0; dst[k] = dir[k]; k++; }
    while(name[j]){ if(k + 1 >= cap) return 0; dst[k++] = name[j++]; }
    dst[k] = 0;
    return 1;
}

/* dir ("PROGRAMS/" ya da "") altindaki name dosyasini acar; klasor degilse 1 */
static int open_in(fat_vol *v, const char *dir, const char *name, fat_file *f){
    char path[PNAME_MAX + 16];
    if(!path_join(path, sizeof path, dir, name)) return 0;
    return fat_open(v, path, f) == 0 && !f->is_dir;
}

/* k. aday ad: 0 = ad olarak, 1 = ad.EXE, 2 = ad.WIND (uzantisiz adlarda) */
static void cand_make(char *dst, const char *name, int k){
    static const char *sfx[3] = { "", ".EXE", ".WIND" };
    unsigned j = 0;
    for(unsigned i = 0; name[i] && j < PNAME_MAX - 8; i++) dst[j++] = name[i];
    for(unsigned i = 0; sfx[k][i] && j < PNAME_MAX - 1; i++) dst[j++] = sfx[k][i];
    dst[j] = 0;
}

/* Programi bulur. Sira: MVOL:PROGRAMS/ (kurulu), MVOL koku, SVOL koku. src_only: PROGRAMS atlanir.
   Uzantisiz adda once ad, sonra .EXE ve .WIND denenir. Donus 1: bulundu (f, vol, cand doldurulur). */
static int prog_lookup(const char *name, int src_only, fat_file *f, fat_vol **vol, char *cand){
    int n = has_dot(name) ? 1 : 3;
    int mok = mvol_ready();
    int sok = (mok && !MVOL_RAM) ? svol_ready() : 0;
    for(int k = 0; k < n; k++){
        cand_make(cand, name, k);
        if(mok && !src_only && open_in(&MVOL, "PROGRAMS/", cand, f)){ *vol = &MVOL; return 1; }
        if(mok && open_in(&MVOL, "", cand, f)){ *vol = &MVOL; return 1; }
        if(sok && open_in(&SVOL, "", cand, f)){ *vol = &SVOL; return 1; }
    }
    return 0;
}

/* Dosyanin tamamini buf'a okur (f yeni acilmis olmali). Donus 0 basarili */
static int file_load(fat_vol *v, fat_file *f, unsigned char *buf){
    unsigned n = 0;
    while(n < f->size){
        int r = fat_read(v, f, buf + n, f->size - n);
        if(r <= 0) return -1;
        n += (unsigned)r;
    }
    return 0;
}

/* Programin turu: 'E' (PE32, "MZ"), 'W' (.wind, "WIND"), 0 taninmadi */
static int img_kind(const unsigned char *b, unsigned n){
    if(n >= 2 && b[0] == 'M' && b[1] == 'Z') return 'E';
    if(n >= 4 && b[0] == 'W' && b[1] == 'I' && b[2] == 'N' && b[3] == 'D') return 'W';
    return 0;
}

/* Ad bir programa aitse turu: 'E', 'W', 0 (taninmadi), -1 (bulunamadi) */
static int prog_kind(const char *name){
    fat_file f; fat_vol *v; char cand[PNAME_MAX];
    unsigned char hd[4];
    if(!prog_lookup(name, 0, &f, &v, cand)) return -1;
    if(f.size == 0) return 0;
    int r = fat_read(v, &f, hd, 4);
    if(r <= 0) return 0;
    return img_kind(hd, (unsigned)r);
}

static void frame_wait(void);   /* asagida tanimli */

static void wa_fill(int x, int y, int w, int h, unsigned c){ fr(x, y, w, h, c); }
static void wa_pixel(int x, int y, unsigned c){ pset(x, y, c); }
static void wa_text(int x, int y, const char *s, unsigned c, int sc){
    if(sc < 1) sc = 1;
    if(sc > 4) sc = 4;
    ds(x, y, s, c, 0, sc);
}
static void wa_present(void){ swap_buffers(); }
static int  wa_key(void){ ps2_pump(); return (int)kbd_poll(); }
static void wa_wait(void){ frame_wait(); }
static unsigned wa_ticks(void){ return TICKS; }

static void wind_exec(unsigned n);

/* "wind DOSYA": programi calistirir; program donene kadar kernel bekler (ring 0, guvenilen kod) */
static void wind_run(const char *path){
    if(!mvol_ready()){ tl_add("DISK YOK VEYA FAT BICIMI TANINMADI"); return; }
    fat_file f; fat_vol *v; char cand[PNAME_MAX];
    if(!prog_lookup(path, 0, &f, &v, cand)){ tl_add("DOSYA BULUNAMADI"); return; }
    if(f.size > WIND_HDR_SIZE + WIND_MAX_MEM){ tl_add("WIND: DOSYA COK BUYUK"); return; }
    if(file_load(v, &f, WIN_FILE) != 0){ tl_add("WIND: DOSYA OKUNAMADI"); return; }
    wind_exec(f.size);
}

/* WIN_FILE'daki n baytlik .wind imajini ayristirir ve calistirir */
static void wind_exec(unsigned n){
    wind_prog p;
    int e = wind_parse(WIN_FILE, n, &p);
    if(e != WIND_OK){ tl_add(wind_err_text(e)); return; }

    for(unsigned i = 0; i < p.mem_size; i++) WIN_MEM[i] = 0;
    for(unsigned i = 0; i < p.code_size; i++) WIN_MEM[i] = p.code[i];

    wind_api api;
    api.version = 1;
    api.fill = wa_fill;
    api.pixel = wa_pixel;
    api.text = wa_text;
    api.present = wa_present;
    api.key = wa_key;
    api.wait = wa_wait;
    api.ticks = wa_ticks;
    api.mem = WIN_MEM;
    api.mem_size = p.mem_size;
    ((wind_entry)(void *)(WIN_MEM + p.entry))(&api);
    tl_add("WIND: PROGRAM BITTI");
}

/* ---- .exe calistirici (PE32 i386) ----
   Programa yalnizca kernel32'nin kucuk bir alt kumesi saglanir. Program ring 0'da
   calisir (guvenilen kod); ExitProcess yigini kernel'e geri alip cagriyi bitirir. */
#define EXE_FILE_MAX (256u * 1024u)
#define EXE_IMG_MAX  (512u * 1024u)
static unsigned char EXE_FILE[EXE_FILE_MAX];
static unsigned char EXE_MEM[EXE_IMG_MAX];
static unsigned EXE_ESP = 0, EXE_RET = 0;    /* kernel yigini ve donus adresi */
static unsigned EXE_CODE = 0;
static char EXE_LINE[TL_W];
static int EXE_LN = 0;

static void exe_flush(void){
    EXE_LINE[EXE_LN] = 0;
    tl_add(EXE_LINE);
    EXE_LN = 0;
}
/* WriteFile ciktisi: satirlara bolunup terminale eklenir */
static void exe_putc(char c){
    if(c == '\r') return;
    if(c == '\n'){ exe_flush(); return; }
    if(EXE_LN >= TL_W - 2) exe_flush();
    EXE_LINE[EXE_LN++] = c;
}

/* kernel32 alt kumesi (stdcall: cagiran degil cagrilan temizler) */
static void *__attribute__((stdcall)) exe_GetStdHandle(unsigned id){
    return (void *)(unsigned)(0x100u + (id & 0xFFu));     /* sahte tutamaclar */
}
static int __attribute__((stdcall)) exe_WriteFile(void *h, const unsigned char *buf, unsigned n,
                                                  unsigned *written, void *ov){
    (void)h; (void)ov;
    for(unsigned i = 0; i < n; i++) exe_putc((char)buf[i]);
    if(written) *written = n;
    return 1;
}
static unsigned __attribute__((stdcall)) exe_GetTickCount(void){ return (unsigned)TICKS; }
static void __attribute__((stdcall, noreturn)) exe_ExitProcess(unsigned code){
    EXE_CODE = code;
    __asm__ volatile("movl %0, %%esp\n\tjmp *%1" : : "m"(EXE_ESP), "m"(EXE_RET));
    for(;;) { }
}

static int exe_name_is(const char *a, const char *b){
    for(;;){
        char x = *a++, y = *b++;
        if(x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if(y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if(x != y) return 0;
        if(!x) return 1;
    }
}

static unsigned exe_resolve(void *ctx, const char *dll, const char *name, unsigned ord){
    (void)ctx;
    if(!dll || !name || ord) return 0;                 /* yalnizca adla import */
    if(!exe_name_is(dll, "kernel32.dll")) return 0;
    if(exe_name_is(name, "ExitProcess"))    return (unsigned)(void *)exe_ExitProcess;
    if(exe_name_is(name, "GetStdHandle"))   return (unsigned)(void *)exe_GetStdHandle;
    if(exe_name_is(name, "WriteFile"))      return (unsigned)(void *)exe_WriteFile;
    if(exe_name_is(name, "GetTickCount"))   return (unsigned)(void *)exe_GetTickCount;
    return 0;
}

/* Giris noktasini cagirir. Donus adresi ve yigin kaydedilir; ExitProcess bunlara doner.
   Callee-saved yazmaclar (ebx, esi, edi, ebp) cagri oncesi ve sonrasi ayni kalir. */
static void exe_call(unsigned entry){
    __asm__ volatile(
        "pushl %%ebx\n\t"
        "pushl %%esi\n\t"
        "pushl %%edi\n\t"
        "pushl %%ebp\n\t"
        "movl $1f, %0\n\t"
        "movl %%esp, %1\n\t"
        "call *%2\n"
        "1:\n\t"
        "popl %%ebp\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebx\n\t"
        : "=m"(EXE_RET), "=m"(EXE_ESP)
        : "r"(entry)
        : "eax", "ecx", "edx", "memory", "cc");
}

/* "exe DOSYA": PE32 programini yukler, kernel32 importlarini baglar ve calistirir */
static void exe_exec(unsigned n);

static void exe_run(const char *path){
    if(!mvol_ready()){ tl_add("DISK YOK VEYA FAT BICIMI TANINMADI"); return; }
    fat_file f; fat_vol *v; char cand[PNAME_MAX];
    if(!prog_lookup(path, 0, &f, &v, cand)){ tl_add("DOSYA BULUNAMADI"); return; }
    if(f.size > EXE_FILE_MAX){ tl_add("EXE: DOSYA COK BUYUK"); return; }
    if(file_load(v, &f, EXE_FILE) != 0){ tl_add("EXE: DOSYA OKUNAMADI"); return; }
    exe_exec(f.size);
}

/* EXE_FILE'daki n baytlik PE imajini yukler, kernel32 importlarini baglar ve calistirir */
static void exe_exec(unsigned n){
    pe_info info;
    unsigned entry = 0;
    int e = pe_load(EXE_FILE, n, EXE_MEM, EXE_IMG_MAX, (unsigned)(void *)EXE_MEM,
                    exe_resolve, 0, &info, &entry);
    if(e != PE_OK){ tl_add(pe_err_text(e)); return; }

    EXE_LN = 0;
    EXE_CODE = 0;
    exe_call(entry);
    if(EXE_LN > 0) exe_flush();
    char msg[TL_W], part[TL_W], num[16];
    itoa((int)EXE_CODE, num);
    app_str(part, "EXE: PROGRAM BITTI (KOD ", num);
    app_str(msg, part, ")");
    tl_add(msg);
}

/* ---- Kurulum: "kur DOSYA" programi kaynaktan PROGRAMS klasorune kopyalar.
   Kurulu programlar "uygulamalar" ile listelenir, Spotlight'ta gorunur, adlariyla calisir. ---- */
static void sp_refresh(void);   /* Spotlight bolumunde tanimli */

static void tl_cat(char *b, const char *s){
    int k = 0;
    while(b[k]) k++;
    for(int i = 0; s[i] && k < TL_W - 1; i++) b[k++] = s[i];
    b[k] = 0;
}

/* .EXE ya da .WIND ile biten ad mi? (buyuk/kucuk harf farketmez) */
static int prog_name_ok(const char *s){
    int n = 0;
    while(s[n]) n++;
    if(n >= 4 && exe_name_is(s + n - 4, ".exe")) return 1;
    if(n >= 5 && exe_name_is(s + n - 5, ".wind")) return 1;
    return 0;
}

/* ad bir programsa calistirir (exe / wind). Program yoksa 0 doner, hicbir sey yazmaz */
static int prog_try(const char *name){
    int k = prog_kind(name);
    if(k < 0) return 0;
    if(k == 'E') exe_run(name);
    else if(k == 'W') wind_run(name);
    else tl_add("BU DOSYA CALISTIRILAMAZ (MZ ya da WIND basligi yok)");
    return 1;
}

static void uyg_cb(void *ctx, const char *name, unsigned is_dir, unsigned size){
    char b[TL_W], num[16];
    (*(int *)ctx)++;
    b[0] = 0;
    tl_cat(b, "  ");
    tl_cat(b, name);
    if(is_dir){ tl_cat(b, "  <KLASOR>"); }
    else {
        itoa((int)size, num);
        tl_cat(b, "  ");
        tl_cat(b, num);
        tl_cat(b, " BAYT");
    }
    tl_add(b);
}

/* "uygulamalar": PROGRAMS klasorundeki kurulu programlari listeler */
static void uyg_list(void){
    int cnt = 0;
    fat_file d;
    if(!mvol_ready()){ tl_add("DISK YOK: UYGULAMA LISTESI ALINAMADI"); return; }
    if(fat_open(&MVOL, "PROGRAMS", &d) != 0 || !d.is_dir){ tl_add("KURULU UYGULAMA YOK  (kur DOSYA ile kurun)"); return; }
    tl_add("KURULU UYGULAMALAR:");
    fat_list(&MVOL, "PROGRAMS", uyg_cb, &cnt);
    if(cnt == 0) tl_add("  (bos)");
}

/* kaynak kok dizindeki kurulabilir programlar */
static void kur_list_cb(void *ctx, const char *name, unsigned is_dir, unsigned size){
    char b[TL_W];
    (void)size;
    if(is_dir || !prog_name_ok(name)) return;
    (*(int *)ctx)++;
    b[0] = 0;
    tl_cat(b, "  ");
    tl_cat(b, name);
    tl_add(b);
}

static void kur_list(void){
    int cnt = 0;
    fat_vol *src;
    if(!mvol_ready()){ tl_add("DISK YOK: KURULABILIR PROGRAM LISTESI ALINAMADI"); return; }
    src = (!MVOL_RAM && svol_ready()) ? &SVOL : &MVOL;
    tl_add("KURULABILIR PROGRAMLAR (kur DOSYA ile kurun):");
    fat_list(src, "", kur_list_cb, &cnt);
    if(cnt == 0) tl_add("  (yok)");
}

/* MVOL'daki path dosyasi want ile birebir ayni mi? (diskten geri okunur) */
static int verify_copy(const char *path, const unsigned char *want, unsigned n){
    fat_file f;
    unsigned char chunk[512];
    unsigned pos = 0;
    if(fat_open(&MVOL, path, &f) != 0 || f.is_dir || f.size != n) return 0;
    while(pos < n){
        unsigned take = (n - pos < sizeof chunk) ? n - pos : (unsigned)sizeof chunk;
        int r = fat_read(&MVOL, &f, chunk, take);
        if(r <= 0) return 0;
        for(int i = 0; i < r; i++) if(chunk[i] != want[pos + i]) return 0;
        pos += (unsigned)r;
    }
    return 1;
}

/* "kur": bos ise kurulabilirleri listeler; "kur DOSYA" programi PROGRAMS klasorune kurar */
static void kur_cmd(const char *arg){
    fat_file src, d, ex;
    fat_vol *sv;
    char name[PNAME_MAX], dst[PNAME_MAX + 16], b[TL_W], num[16];
    int kind;
    if(!arg[0]){ kur_list(); return; }
    if(!mvol_ready()){ tl_add("KURULUM ICIN DISK YOK (ATA ya da RAM disk gerekli)"); return; }
    if(!prog_lookup(arg, 1, &src, &sv, name)){ tl_add("KAYNAKTA BOYLE BIR PROGRAM YOK"); return; }
    if(src.size == 0 || src.size > sizeof WIN_FILE){ tl_add("KUR: PROGRAM BOYUTU UYGUN DEGIL"); return; }
    if(file_load(sv, &src, WIN_FILE) != 0){ tl_add("KUR: KAYNAK OKUNAMADI"); return; }
    kind = img_kind(WIN_FILE, src.size);
    if(kind == 'E'){
        pe_info info;
        int e = pe_parse(WIN_FILE, src.size, &info);
        if(e != PE_OK){ tl_add(pe_err_text(e)); return; }
    } else if(kind == 'W'){
        wind_prog p;
        int e = wind_parse(WIN_FILE, src.size, &p);
        if(e != WIND_OK){ tl_add(wind_err_text(e)); return; }
    } else {
        tl_add("KUR: GECERSIZ PROGRAM (MZ ya da WIND basligi yok)");
        return;
    }
    /* PROGRAMS klasoru yoksa olustur */
    if(fat_open(&MVOL, "PROGRAMS", &d) != 0){
        if(fat_mkdir(&MVOL, "PROGRAMS") != 0){ tl_add("KUR: PROGRAMS KLASORU OLUSTURULAMADI"); return; }
    } else if(!d.is_dir){ tl_add("KUR: PROGRAMS ADLI BIR DOSYA VAR"); return; }
    if(!path_join(dst, sizeof dst, "PROGRAMS/", name)){ tl_add("KUR: AD COK UZUN"); return; }
    if(fat_open(&MVOL, dst, &ex) == 0){
        b[0] = 0; tl_cat(b, "ZATEN KURULU: "); tl_cat(b, name); tl_add(b);
        return;
    }
    if(fat_create(&MVOL, dst, WIN_FILE, src.size) != 0){ tl_add("KUR: YAZILAMADI (YER YOK ya da DISK HATASI)"); return; }
    if(!verify_copy(dst, WIN_FILE, src.size)){ tl_add("KUR: DOGRULAMA BASARISIZ"); return; }
    sp_refresh();
    b[0] = 0;
    tl_cat(b, "KURULDU: "); tl_cat(b, name); tl_cat(b, "  (");
    itoa((int)src.size, num); tl_cat(b, num); tl_cat(b, " BAYT)");
    tl_add(b);
    if(MVOL_RAM) tl_add("UYARI: RAM DISKE KURULDU, YENIDEN BASLATINCA SILINIR");
}

/* ---- DOSYA GEZGINI: KAYNAK (kurulabilir dosyalar) ve UYGULAMALAR (PROGRAMS) sekmeleri.
   Dosyayi secip KUR ile kurulur, CALISTIR ile calisir. Cift tiklama ayni islemi yapar.
   Durum satiri son terminal satirini gosterir. ---- */
#define FM_MAX  32
#define FM_ROWS 12
static char FM_NAME[FM_MAX][FAT_NAME_MAX + 1];
static unsigned FM_SIZE[FM_MAX], FM_DIR[FM_MAX];
static int FM_N = 0, FM_SEL = -1, FM_TAB = 0, FM_VALID = 0;
static u32 FM_LAST_T = 0;

static void fm_cb(void *ctx, const char *name, unsigned is_dir, unsigned size){
    int k = 0;
    (void)ctx;
    if(FM_N >= FM_MAX) return;
    while(name[k] && k < FAT_NAME_MAX){ FM_NAME[FM_N][k] = name[k]; k++; }
    FM_NAME[FM_N][k] = 0;
    FM_SIZE[FM_N] = size;
    FM_DIR[FM_N] = is_dir;
    FM_N++;
}

static void fm_refresh(void){
    FM_N = 0; FM_SEL = -1;
    if(!mvol_ready()) return;
    if(FM_TAB == 0){
        fat_vol *src = (!MVOL_RAM && svol_ready()) ? &SVOL : &MVOL;
        fat_list(src, "", fm_cb, 0);
    } else {
        fat_list(&MVOL, "PROGRAMS", fm_cb, 0);
    }
}

/* secili dosya icin islem: KAYNAK sekmesinde kur, UYGULAMALAR sekmesinde calistir */
static void fm_act(void){
    if(FM_SEL < 0 || FM_SEL >= FM_N){ tl_add("DOSYA GEZGINI: ONCE BIR DOSYA SECIN"); return; }
    if(FM_DIR[FM_SEL] || !prog_name_ok(FM_NAME[FM_SEL])){
        tl_add("DOSYA GEZGINI: BU DOSYA KURULAMAZ / CALISTIRILAMAZ (.exe ya da .wind gerekli)");
        return;
    }
    if(FM_TAB == 0){
        kur_cmd(FM_NAME[FM_SEL]);
        fm_refresh();
    } else {
        TERM_OPEN = 1; FOCUS = 1;
        prog_try(FM_NAME[FM_SEL]);
    }
}

static const char *fm_status(void){ return TL_N > 0 ? TLINES[TL_N - 1] : ""; }

static void FILEMGR(void){
    i32 fw = 700, fh = 450, fx = FX, fy = FY;
    static const char *TAB_NM[2] = { "KAYNAK", "UYGULAMALAR" };
    if(!FO){ FM_VALID = 0; return; }
    if(!FM_VALID){ fm_refresh(); FM_VALID = 1; }
    if(!FD && MLB && !PMLB && MY >= fy && MY < fy + 35 && MX >= fx && MX < fx + fw - 40){ FD = 1; FDX = MX - fx; FDY = MY - fy; }
    if(FD){
        if(MLB){
            FX = MX - FDX; FY = MY - FDY;
            if(FX < 0) FX = 0;
            if(FY < 0) FY = 0;
            if(FX > (i32)SW - fw) FX = (i32)SW - fw;
            if(FY > (i32)SH - fh) FY = (i32)SH - fh;
        } else FD = 0;
    }
    DRAW_WINDOW(fx, fy, fw, fh, "DOSYA GEZGINI", PAN_BG);

    for(int t = 0; t < 2; t++){
        i32 tx = fx + 20 + t * 160, ty = fy + 46;
        rr(tx, ty, 150, 28, 8, t == FM_TAB ? WIN_BLUE : 0xFF2A303Cu);
        ds(tx + 14, ty + 10, TAB_NM[t], CW, 0, 1);
        if(CLK(tx, ty, 150, 28) && FM_TAB != t){ FM_TAB = t; fm_refresh(); }
    }
    ds(fx + 20, fy + 84, FM_TAB == 0 ? "KAYNAK: /" : "UYGULAMALAR: /PROGRAMS", CGY, 0, 1);

    i32 ly = fy + 106;
    int shown = FM_N < FM_ROWS ? FM_N : FM_ROWS;
    if(FM_N == 0) ds(fx + 24, ly + 6, "BU KLASORDE DOSYA YOK", CGY, 0, 1);
    for(int r = 0; r < shown; r++){
        i32 ry = ly + r * 22;
        int sel = (r == FM_SEL);
        if(sel) rr(fx + 16, ry, fw - 32, 20, 6, WIN_BLUE);
        ds(fx + 24, ry + 6, FM_NAME[r], sel ? CW : CTXT, 0, 1);
        if(FM_DIR[r]){
            ds(fx + fw - 150, ry + 6, "KLASOR", sel ? CW : CGY, 0, 1);
        } else {
            char num[16], sz[TL_W];
            itoa((int)FM_SIZE[r], num);
            sz[0] = 0; tl_cat(sz, num); tl_cat(sz, " BAYT");
            ds(fx + fw - 150, ry + 6, sz, sel ? CW : CGY, 0, 1);
        }
        if(CLK(fx + 16, ry, fw - 32, 20)){
            int again = (FM_SEL == r && TICKS - FM_LAST_T < 30);
            FM_SEL = r; FM_LAST_T = TICKS;
            if(again){ FM_LAST_T = 0; fm_act(); }
        }
    }

    i32 by = fy + fh - 44;
    rr(fx + 20, by, 130, 30, 8, WIN_BLUE);
    ds(fx + 36, by + 10, FM_TAB == 0 ? "KUR" : "CALISTIR", CW, 0, 1);
    if(CLK(fx + 20, by, 130, 30)) fm_act();
    rr(fx + 170, by, 130, 30, 8, 0xFF2A303Cu);
    ds(fx + 196, by + 10, "YENILE", CTXT, 0, 1);
    if(CLK(fx + 170, by, 130, 30)) fm_refresh();
    ds(fx + 20, fy + fh - 74, fm_status(), CGN, 0, 1);
}

static void fw_status(void){
    char b[TL_W], num[16];
    b[0] = 0; tl_cat(b, "GUVENLIK DUVARI: "); tl_cat(b, FW_ON ? "ACIK" : "KAPALI");
    tl_add(b);
    b[0] = 0; tl_cat(b, "ENGELLI IP: "); itoa(FW_BLK_N, num); tl_cat(b, num);
    tl_cat(b, "   IZINLI PORT: "); itoa(FW_ALLOW_N, num); tl_cat(b, num);
    tl_cat(b, "   DUSURULEN PAKET: "); itoa((int)FW_DROP, num); tl_cat(b, num);
    tl_add(b);
}

/* "guvenlik [ac|kapa|engelle IP|kaldir IP|izin PORT|liste]" */
static void fw_cmd(const char *arg){
    char w[16], ip[16], b[TL_W], num[16];
    int k = 0;
    const char *p = arg;
    u32 v = 0;
    while(*p == ' ') p++;
    while(*p && *p != ' ' && k < 15) w[k++] = *p++;
    w[k] = 0;
    while(*p == ' ') p++;
    if(w[0] == 0){ fw_status(); return; }
    if(str_eq(w, "ac")){ FW_ON = 1; tl_add("GUVENLIK DUVARI ACILDI"); return; }
    if(str_eq(w, "kapa")){ FW_ON = 0; tl_add("GUVENLIK DUVARI KAPATILDI (KORUMASIZ)"); return; }
    if(str_eq(w, "engelle")){
        if(!parse_ip(p, &v)){ tl_add("KULLANIM: guvenlik engelle a.b.c.d"); return; }
        if(FW_BLK_N >= FW_BLK_MAX){ tl_add("ENGEL LISTESI DOLU"); return; }
        FW_BLK[FW_BLK_N++] = v;
        ip_str(v, ip);
        b[0] = 0; tl_cat(b, "ENGELLENDI: "); tl_cat(b, ip); tl_add(b);
        return;
    }
    if(str_eq(w, "kaldir")){
        if(!parse_ip(p, &v)){ tl_add("KULLANIM: guvenlik kaldir a.b.c.d"); return; }
        for(int i = 0; i < FW_BLK_N; i++) if(FW_BLK[i] == v){
            for(int j = i; j + 1 < FW_BLK_N; j++) FW_BLK[j] = FW_BLK[j + 1];
            FW_BLK_N--;
            tl_add("ENGEL KALDIRILDI");
            return;
        }
        tl_add("BU IP ENGELLI DEGIL");
        return;
    }
    if(str_eq(w, "izin")){
        int d = 0;
        while(*p >= '0' && *p <= '9' && v < 65536){ v = v * 10 + (u32)(*p - '0'); p++; d++; }
        if(!d || v == 0 || v > 65535 || *p){ tl_add("KULLANIM: guvenlik izin PORT  (1-65535)"); return; }
        if(FW_ALLOW_N >= FW_ALLOW_MAX){ tl_add("IZIN LISTESI DOLU"); return; }
        FW_ALLOW[FW_ALLOW_N++] = v;
        b[0] = 0; tl_cat(b, "PORT IZINLI: "); itoa((int)v, num); tl_cat(b, num); tl_add(b);
        return;
    }
    if(str_eq(w, "liste")){
        tl_add("ENGELLI IP'LER:");
        for(int i = 0; i < FW_BLK_N; i++){ ip_str(FW_BLK[i], ip); b[0] = 0; tl_cat(b, "  "); tl_cat(b, ip); tl_add(b); }
        tl_add("IZINLI PORTLAR (SYN):");
        for(int i = 0; i < FW_ALLOW_N; i++){ itoa((int)FW_ALLOW[i], num); b[0] = 0; tl_cat(b, "  "); tl_cat(b, num); tl_add(b); }
        return;
    }
    tl_add("KULLANIM: guvenlik [ac|kapa|engelle IP|kaldir IP|izin PORT|liste]");
}

static void term_exec(void){
    char line[TL_W];
    int n = TIN_LEN;
    for(int i = 0; i < n; i++) line[i] = TIN[i];
    line[n] = 0;

    char pr[TL_W];
    app_str(pr, PROMPT_STR, line);
    tl_add(pr);
    TIN_LEN = 0;

    int i = 0, c = 0;
    char cmd[TL_W];
    while(i < n && line[i] == ' ') i++;
    while(i < n && line[i] != ' ' && c < TL_W - 1) cmd[c++] = line[i++];
    cmd[c] = 0;
    while(i < n && line[i] == ' ') i++;
    const char *arg = &line[i];
    if(c == 0) return;

    if(str_eq(cmd, "help")){ tl_add("KOMUTLAR: help ver mem echo clear time muzik miku not snake web exit lspci"); tl_add("AG: ping [ip]  (ornek: ping 10.0.2.2)"); tl_add("SES: ses [hz]  (ornek: ses 440)"); tl_add("WIND: wind DOSYA  (ornek: wind HELLO.WIND)"); tl_add("EXE: exe DOSYA  (ornek: exe PE_HELLO.EXE)"); tl_add("KUR: kur [DOSYA]  uygulamalar  (ornek: kur PE_HELLO.EXE)"); tl_add("Kurulu programi adiyla calistir (ornek: pe_hello)"); tl_add("GUVENLIK: guvenlik [ac|kapa|engelle IP|kaldir IP|izin PORT|liste]"); tl_add("HTTP: http ip[:port][/yol]"); }
    else if(str_eq(cmd, "ver"))   tl_add("WindOS 18.1 (x86, 32-bit)");
    else if(str_eq(cmd, "mem")){
        char b[TL_W]; kcpy(b, "FIZIKSEL RAM (MULTIBOOT): ");
        if(SYS_RAM_MB){ itoa((int)SYS_RAM_MB, b + klen(b)); kcpy(b + klen(b), " MB"); }
        else kcpy(b + klen(b), "BILINMIYOR");
        tl_add(b);
    }
    else if(str_eq(cmd, "echo"))  tl_add(arg);
    else if(str_eq(cmd, "clear")) TL_N = 0;
    else if(str_eq(cmd, "time"))  tl_add("SAAT DONANIMI (RTC) HENUZ SURUCU DEGIL.");
    else if(str_eq(cmd, "muzik")){ MUS_OPEN = 1; mus_start(0); }
    else if(str_eq(cmd, "miku"))  MIKU_OPEN = 1;
    else if(str_eq(cmd, "not"))   { NOTE_OPEN = 1; FOCUS = 2; }
    else if(str_eq(cmd, "snake")) snake_open();
    else if(str_eq(cmd, "web"))   { BR_OPEN = 1; FOCUS = 4; }
    else if(str_eq(cmd, "exit"))  TERM_OPEN = 0;
    else if(str_eq(cmd, "lspci")) pci_list_to_term();
    else if(str_eq(cmd, "ping"))  ping_cmd(arg);
    else if(str_eq(cmd, "ses"))   ses_cmd(arg);
    else if(str_eq(cmd, "http"))  http_cmd(arg);
    else if(str_eq(cmd, "wind"))  wind_run(arg);
    else if(str_eq(cmd, "exe"))   exe_run(arg);
    else if(str_eq(cmd, "kur"))   kur_cmd(arg);
    else if(str_eq(cmd, "uygulamalar")) uyg_list();
    else if(str_eq(cmd, "guvenlik")) fw_cmd(arg);
    else if(prog_try(cmd)) { }
    else {
        char b[TL_W];
        app_str(b, "BILINMEYEN KOMUT: ", cmd);
        tl_add(b);
    }
}

static void term_key(u8 c){
    if(c == 8){ if(TIN_LEN > 0) TIN_LEN--; return; }
    if(c == '\n' || c == '\r'){ term_exec(); return; }
    if(c >= 32 && c < 127 && TIN_LEN < 60) TIN[TIN_LEN++] = (char)c;
}

static void term_init(void){
    tl_add("WindOS 18.1. 'help' yazip Enter'a bas.");
}

static void TERMINAL_APP(void){
    if(!TERM_OPEN) return;
    i32 cw = 650, ch = 450, cx = CX, cy = CY;
    if(!CD && MLB && !PMLB && MY >= cy && MY < cy + 35 && MX >= cx && MX < cx + cw - 40){ CD = 1; CDX = MX - cx; CDY = MY - cy; }
    if(CD){
        if(MLB){
            CX = MX - CDX; CY = MY - CDY;
            if(CX < 0) CX = 0;
            if(CY < 0) CY = 0;
            if(CX > (i32)SW - cw) CX = (i32)SW - cw;
            if(CY > (i32)SH - ch) CY = (i32)SH - ch;
        } else CD = 0;
    }
    DRAW_WINDOW(cx, cy, cw, ch, "WIND-TERM (LINUX TERMINAL)", SAFE_BLK);
    if(CLK(cx + 14, cy + 11, 12, 12)){ TERM_OPEN = 0; return; }
    if(MLB && !PMLB && HOV(cx, cy, cw, ch)) FOCUS = 1;

    const i32 visible = (ch - 80) / 12;
    const i32 start = TL_N > visible ? TL_N - visible : 0;
    for(i32 i = start; i < TL_N; i++) ds(cx + 16, cy + 46 + (i - start) * 12, TLINES[i], CTXT, 0, 1);

    char pr[TL_W];
    app_str(pr, PROMPT_STR, "");
    int k = (int)klen(pr);
    for(int i = 0; i < TIN_LEN && k < TL_W - 2; i++) pr[k++] = TIN[i];
    if(FOCUS == 1 && k < TL_W - 1) pr[k++] = '_';
    pr[k] = 0;
    ds(cx + 16, cy + ch - 26, pr, CGN, 0, 1);
}

/* ---- HTML: basit ayristirici (baslik, paragraf, baslik etiketi, baglanti, liste) ---- */
#define BR_MAXL 240
#define BR_COLS 58
static char BR_LN[BR_MAXL][BR_COLS + 1];
static u8   BR_KD[BR_MAXL];            /* 0 metin, 1 baslik, 2 baglanti */
static int  BR_NL = 0;
static int  BR_SCROLL = 0;
static char BR_TITLE[64];
static char BR_CUR[BR_COLS + 1];
static int  BR_CN = 0;
static u8   BR_CK = 0;
static u8   BR_STYLE = 0;

static void br_flush(void){
    while(BR_CN > 0 && BR_CUR[BR_CN - 1] == ' ') BR_CN--;
    if(BR_CN > 0 && BR_NL < BR_MAXL){
        for(int i = 0; i < BR_CN; i++) BR_LN[BR_NL][i] = BR_CUR[i];
        BR_LN[BR_NL][BR_CN] = 0;
        BR_KD[BR_NL] = BR_CK;
        BR_NL++;
    }
    BR_CN = 0;
}

/* Blok sonu: satir bitir, arada bos satir birak */
static void br_block(void){
    br_flush();
    if(BR_NL > 0 && BR_LN[BR_NL - 1][0] && BR_NL < BR_MAXL){
        BR_LN[BR_NL][0] = 0; BR_KD[BR_NL] = 0; BR_NL++;
    }
}

static void br_putc(char c){
    if(c == ' ' && (BR_CN == 0 || BR_CUR[BR_CN - 1] == ' ')) return;
    if(BR_CN == 0) BR_CK = BR_STYLE;
    if(BR_CN >= BR_COLS){
        br_flush();
        if(c == ' ') return;
    }
    BR_CUR[BR_CN++] = c;
}

static void html_parse(const u8 *s, int n){
    int skip = 0, in_title = 0, tl = 0;
    char skipname[10] = {0};
    BR_NL = 0; BR_CN = 0; BR_STYLE = 0; BR_SCROLL = 0; BR_TITLE[0] = 0;
    int i = 0;
    while(i < n){
        u8 c = s[i];
        if(c == '<'){
            int j = i + 1, closing = 0, nl = 0;
            char name[10];
            if(j < n && s[j] == '/'){ closing = 1; j++; }
            while(j < n && nl < 9 && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= '0' && s[j] <= '9'))){
                char ch = (char)s[j];
                if(ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                name[nl++] = ch; j++;
            }
            name[nl] = 0;
            while(j < n && s[j] != '>') j++;
            i = j + 1;
            if(skip){ if(closing && str_eq(name, skipname)) skip = 0; continue; }
            if(!closing && (str_eq(name, "script") || str_eq(name, "style"))){
                skip = 1; kcpy(skipname, name); continue;
            }
            if(str_eq(name, "title")){ in_title = !closing; continue; }
            if(str_eq(name, "br")){ br_flush(); continue; }
            if(str_eq(name, "p") || str_eq(name, "div") || str_eq(name, "ul") || str_eq(name, "ol") || str_eq(name, "tr") || str_eq(name, "table")){
                br_block(); continue;
            }
            if(str_eq(name, "li")){
                br_block();
                if(!closing){ br_putc('-'); br_putc(' '); }
                continue;
            }
            if(name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]){
                br_block(); BR_STYLE = closing ? 0 : 1;
                if(closing) br_block();
                continue;
            }
            if(str_eq(name, "a")){ BR_STYLE = closing ? 0 : 2; continue; }
            continue;
        }
        if(skip){ i++; continue; }
        if(in_title){
            if(c >= 32 && c < 127 && tl < 60){ BR_TITLE[tl++] = (char)c; BR_TITLE[tl] = 0; }
            i++; continue;
        }
        if(c == '&'){
            int j = i + 1, el = 0;
            char en[8];
            while(j < n && el < 7 && s[j] != ';' && s[j] != ' ' && s[j] != '<'){ en[el++] = (char)s[j]; j++; }
            en[el] = 0;
            if(j < n && s[j] == ';'){
                char out = 0;
                if(str_eq(en, "amp")) out = '&';
                else if(str_eq(en, "lt")) out = '<';
                else if(str_eq(en, "gt")) out = '>';
                else if(str_eq(en, "quot")) out = '"';
                else if(str_eq(en, "nbsp")) out = ' ';
                else if(str_eq(en, "apos")) out = '\'';
                if(out){ br_putc(out); i = j + 1; continue; }
            }
            br_putc('&'); i++; continue;
        }
        if(c == '\n' || c == '\r' || c == '\t') br_putc(' ');
        else if(c >= 32 && c < 127) br_putc((char)c);
        else if(c >= 128){                       /* UTF-8 cok baytli: tek '?' */
            if(BR_CN == 0 || BR_CUR[BR_CN - 1] != '?') br_putc('?');
        }
        i++;
    }
    br_flush();
}

/* TCP_RX icindeki yanitin govdesini ayristirir */
static void html_from_response(void){
    int b = 0;
    for(int i = 0; i + 3 < TCP_RXN; i++){
        if(TCP_RX[i] == '\r' && TCP_RX[i + 1] == '\n' && TCP_RX[i + 2] == '\r' && TCP_RX[i + 3] == '\n'){ b = i + 4; break; }
    }
    html_parse(TCP_RX + b, TCP_RXN - b);
}

/* ---- TARAYICI: IP adresi ile sayfa acar (ornek 10.0.2.2:8080/index.html) ---- */
static char BR_URL[64]; static int BR_ULEN = 0;
static char BR_TARGET[64];
static char BR_ERR[64];
static int BR_PAGE = 0; /* 0 ana sayfa, 1 hata, 2 hakkinda, 3 metin sayfasi, 4 Chromium goruntusu */

/* ---- Chromium koprusu: host'ta calisan tools/chromium_bridge.py sayfayi
   Chromium ile acip ekran goruntusunu 24-bit BMP olarak yollar ---- */
#define BR_WW 760                /* tarayici penceresi genisligi */
#define BR_WH 520                /* tarayici penceresi yuksekligi */
#define BR_VW (BR_WW - 40)       /* sayfa alani (viewport) genisligi */
#define BR_VH (BR_WH - 112)      /* sayfa alani (viewport) yuksekligi */
#define BR_PORT 8088             /* koprunun portu (--port ile degisir) */
static u32 BR_IMG[BR_VW * BR_VH]; /* son ekran goruntusu, 0xAARRGGBB */
static int BR_IW = 0, BR_IH = 0;  /* goruntu boyutu (0 = yok) */
/* Video modu (YouTube): host kucuk bir viewport'tan kare yollar, biz 2 kat buyuturuz.
   Kucuk kare = daha az bayt = daha akici kare hizi. */
#define BR_VSW 360
#define BR_VSH 202
#define BR_VSC 2
static int BR_VIDEO = 0;          /* 1: su anki sayfa video sitesi */
static u32 BR_LAST = 0;           /* son kare yenilemesinin TICKS degeri */

static int has_sub(const char *s, const char *sub){
    for(int i = 0; s[i]; i++){
        int m = 0;
        while(sub[m] && s[i + m] == sub[m]) m++;
        if(!sub[m]) return 1;
    }
    return 0;
}

static u32 le32(const u8 *p){ return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 le16(const u8 *p){ return (u16)(p[0] | (p[1] << 8)); }

/* Sorgu degeri icin yuzde kodlama (dst en az 3*strlen(src)+1 bayt) */
static void pct_enc(char *dst, const char *src){
    static const char hx[] = "0123456789ABCDEF";
    int k = 0;
    for(int i = 0; src[i]; i++){
        u8 c = (u8)src[i];
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.' || c == '~';
        if(safe){ dst[k++] = (char)c; }
        else { dst[k++] = '%'; dst[k++] = hx[c >> 4]; dst[k++] = hx[c & 15]; }
    }
    dst[k] = 0;
}

/* 24-bit sikistirmasiz BMP -> BR_IMG (alt-ust sirali her iki yonu de kabul eder) */
static int bmp_decode(const u8 *b, int n){
    if(n < 54 || b[0] != 'B' || b[1] != 'M') return 0;
    u32 off = le32(b + 10);
    i32 w = (i32)le32(b + 18), h = (i32)le32(b + 22);
    u16 bpp = le16(b + 28);
    u32 comp = le32(b + 30);
    if(bpp != 24 || comp != 0 || w <= 0 || h == 0) return 0;
    int top_down = (h < 0);
    if(top_down) h = -h;
    if(w > BR_VW || h > BR_VH) return 0;
    u32 stride = ((u32)w * 3u + 3u) & ~3u;
    if(off > (u32)n || stride * (u32)h > (u32)n - off) return 0;
    for(int y = 0; y < h; y++){
        const u8 *row = b + off + (top_down ? (u32)y : (u32)(h - 1 - y)) * stride;
        for(int x = 0; x < w; x++){
            const u8 *p = row + x * 3;
            BR_IMG[y * BR_VW + x] = 0xFF000000u | ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
        }
    }
    BR_IW = w; BR_IH = h;
    return 1;
}

/* Koprue istek: q = "a=go&u=..." gibi (zaten kodlanmis) sorgu.
   Donus: 1 goruntu hazir, 0 kopru yok (baglanti kurulamadi), -1 kopru hata dondu (BR_ERR dolu) */
static int bridge_req(const char *q){
    char path[512];
    kcpy(path, "/act?");
    int k = (int)klen(path);
    for(int i = 0; q[i] && k < 430; i++) path[k++] = q[i];
    path[k] = 0;
    kcpy(path + k, "&w="); k = (int)klen(path);
    dec_str(BR_VIDEO ? BR_VSW : BR_VW, path + k);
    k = (int)klen(path);
    kcpy(path + k, "&h="); k = (int)klen(path);
    dec_str(BR_VIDEO ? BR_VSH : BR_VH, path + k);

    if(!http_get(NET_GW, BR_PORT, path, "10.0.2.2", 1500)) return 0;
    if(TCP_RXN < 60) { kcpy(BR_ERR, "KOPRU BOS YANIT"); return -1; }

    int hdr_end = -1;
    for(int i = 0; i + 3 < TCP_RXN; i++){
        if(TCP_RX[i] == '\r' && TCP_RX[i + 1] == '\n' && TCP_RX[i + 2] == '\r' && TCP_RX[i + 3] == '\n'){ hdr_end = i + 4; break; }
    }
    if(hdr_end < 0) { kcpy(BR_ERR, "KOPRU GECERSIZ YANIT"); return -1; }

    if(!(TCP_RX[9] == '2' && TCP_RX[10] == '0' && TCP_RX[11] == '0')){   /* 200 degilse govde hata metnidir */
        int n = 0;
        for(int i = hdr_end; i < TCP_RXN && n < 60 && TCP_RX[i] != '\r' && TCP_RX[i] != '\n'; i++)
            BR_ERR[n++] = (char)TCP_RX[i];
        BR_ERR[n] = 0;
        if(n == 0) kcpy(BR_ERR, "KOPRU HATA DONDURDU");
        return -1;
    }

    /* X-Url basligi: gecerli adres adres cubuguna yazilir */
    static const char key[] = "X-Url: ";
    for(int i = 0; i + 7 < hdr_end; i++){
        int m = 0;
        while(m < 7 && TCP_RX[i + m] == (u8)key[m]) m++;
        if(m == 7){
            int n = 0;
            for(int j = i + 7; j < hdr_end && n < 60 && TCP_RX[j] != '\r' && TCP_RX[j] != '\n'; j++)
                BR_URL[n++] = (char)TCP_RX[j];
            BR_URL[n] = 0; BR_ULEN = n;
            break;
        }
    }

    if(!bmp_decode(TCP_RX + hdr_end, TCP_RXN - hdr_end)){ kcpy(BR_ERR, "GORUNTU COZULEMEDI"); return -1; }
    return 1;
}

/* Kopru islemi (kaydirma, tiklama): hata olursa sayfayi hata ekranina alir */
static void br_act(const char *q){
    int r = bridge_req(q);
    if(r == 0){ BR_PAGE = 1; kcpy(BR_ERR, "KOPRU ILE BAGLANTI KOPTU"); }
    else if(r < 0) BR_PAGE = 1;
}

static void br_go(void){
    BR_URL[BR_ULEN] = 0;
    if(BR_ULEN == 0 || str_eq(BR_URL, "home")){ BR_PAGE = 0; return; }
    if(str_eq(BR_URL, "about")){ BR_PAGE = 2; return; }
    /* Kisa yollar: youtube / youtube.com yazinca dogrudan YouTube acilir */
    if(str_eq(BR_URL, "youtube") || str_eq(BR_URL, "youtube.com") || str_eq(BR_URL, "www.youtube.com")){
        kcpy(BR_URL, "https://www.youtube.com/");
        BR_ULEN = (int)klen(BR_URL);
    }
    kcpy(BR_TARGET, BR_URL);
    if(!NIC_UP){ BR_PAGE = 1; kcpy(BR_ERR, "AG KARTI YOK (QEMU -nic user,model=rtl8139)"); return; }

    /* Once Chromium koprusu: JavaScript'li siteler, DNS ve adres biçimi serbest */
    BR_VIDEO = has_sub(BR_URL, "youtube") || has_sub(BR_URL, "youtu.be");
    char q[256];
    kcpy(q, "a=go&u=");
    pct_enc(q + 7, BR_URL);
    int r = bridge_req(q);
    if(r != 0){ BR_PAGE = (r > 0) ? 4 : 1; BR_LAST = TICKS; return; }

    /* Kopru kapaliysa: IP:PORT/YOL adresi icin eski HTTP metin tarayicisi */
    u32 ip = 0; u16 port = 80; char path[96];
    if(!url_parse(BR_URL, &ip, &port, path)){
        BR_PAGE = 1; kcpy(BR_ERR, "KOPRU KAPALI: host'ta tools/chromium_bridge.py calistirin"); return;
    }
    char host[16];
    ip_str(ip, host);
    if(!http_get(ip, port, path, host, 150)){ BR_PAGE = 1; kcpy(BR_ERR, "BAGLANTI KURULAMADI (KOPRU KAPALI MI?)"); return; }
    html_from_response();
    BR_PAGE = 3;
}

static void br_key(u8 c){
    if(BR_PAGE == 4){
        if(c == KEY_UP){ br_act("a=scroll&dy=-200"); return; }
        if(c == KEY_DOWN){ br_act("a=scroll&dy=200"); return; }
        if(BR_VIDEO){
            if(c == KEY_LEFT){ br_act("a=key&k=ArrowLeft"); return; }
            if(c == KEY_RIGHT){ br_act("a=key&k=ArrowRight"); return; }
            if(c == ' '){ br_act("a=key&k=Space"); return; }   /* bosluk URL'de gecersiz: oynat/duraklat */
        }
    }
    if(c == KEY_UP){ if(BR_SCROLL > 0) BR_SCROLL--; return; }
    if(c == KEY_DOWN){ if(BR_SCROLL + 1 < BR_NL) BR_SCROLL++; return; }
    if(c == 8){ if(BR_ULEN > 0) BR_ULEN--; return; }
    if(c == '\n' || c == '\r'){ br_go(); return; }
    if(c >= 32 && c < 127 && BR_ULEN < 60) BR_URL[BR_ULEN++] = (char)c;
}

static void BROWSER_APP(void){
    if(!BR_OPEN) return;
    const i32 wx = 200, wy = 140, ww = BR_WW, wh = BR_WH;
    DRAW_WINDOW(wx, wy, ww, wh, "TARAYICI", PAN_BG);
    if(CLK(wx + 14, wy + 11, 12, 12)){ BR_OPEN = 0; return; }
    if(MLB && !PMLB && HOV(wx, wy, ww, wh)) FOCUS = 4;

    rr(wx + 20, wy + 50, ww - 120, 28, 8, SAFE_BLK);
    char shown[64]; int k = 0;
    for(int i = 0; i < BR_ULEN && k < 60; i++) shown[k++] = BR_URL[i];
    if(FOCUS == 4 && k < 62) shown[k++] = '_';
    shown[k] = 0;
    int hint = (BR_ULEN == 0 && FOCUS != 4);
    ds(wx + 30, wy + 60, hint ? "ADRES (ornek: home, about)" : shown, hint ? CGY : CTXT, 0, 1);
    rr(wx + ww - 90, wy + 50, 70, 28, 8, WIN_BLUE);
    ds(wx + ww - 76, wy + 60, "GIT", CW, 0, 1);
    if(CLK(wx + ww - 90, wy + 50, 70, 28)) br_go();

    rr(wx + 20, wy + 92, ww - 40, wh - 112, 10, PAN_BG);
    if(BR_PAGE == 0){
        ds(wx + 40, wy + 115, "WINDOS TARAYICI", CW, 0, 1);
        ds(wx + 40, wy + 135, "Sayfa adresi yazin (ornek: www.youtube.com) ve Enter'a basin:", CGY, 0, 1);
        ds(wx + 40, wy + 155, "ornek: 10.0.2.2:8080/index.html", CTXT, 0, 1);
        ds(wx + 40, wy + 185, "Yerel ayarlar icin 'about' yazin.", CGY, 0, 1);
    }
    else if(BR_PAGE == 1){
        ds(wx + 40, wy + 115, "BAGLANILAMADI", CRD, 0, 1);
        ds(wx + 40, wy + 135, BR_ERR, CTXT, 0, 1);
        ds(wx + 40, wy + 155, "Adres:", CGY, 0, 1);
        ds(wx + 100, wy + 155, BR_TARGET, CW, 0, 1);
    }
    else if(BR_PAGE == 3){
        if(BR_TITLE[0]){
            char tt[72]; kcpy(tt, "BASLIK: "); kcpy(tt + klen(tt), BR_TITLE);
            ds(wx + 30, wy + 100, tt, CGY, 0, 1);
        }
        int vis = 26;
        for(int r = 0; r < vis && BR_SCROLL + r < BR_NL; r++){
            int idx = BR_SCROLL + r;
            u32 col = (BR_KD[idx] == 1) ? CW : (BR_KD[idx] == 2) ? WIN_BLUE : CTXT;
            ds(wx + 30, wy + 116 + r * 14, BR_LN[idx], col, 0, 1);
        }
        if(BR_NL == 0) ds(wx + 30, wy + 116, "(sayfada metin yok)", CGY, 0, 1);
    }
    else if(BR_PAGE == 4){
        /* Sayfa alani: once tiklamayi isle (sol tik -> koprude tiklama), sonra goruntuyu ciz */
        i32 vx = wx + 20, vy = wy + 92;
        const int sc = BR_VIDEO ? BR_VSC : 1;   /* ekran piksel / sayfa piksel */
        if(HOV(vx, vy, BR_VW, BR_VH) && MLB && !PMLB){
            char q[48];
            kcpy(q, "a=click&x=");
            dec_str((u32)((MX - vx) / sc), q + klen(q));
            kcpy(q + klen(q), "&y=");
            dec_str((u32)((MY - vy) / sc), q + klen(q));
            br_act(q);
        }
        /* Video: kareyi yaklasik 5 tikte bir (~10 kare/sn hedefi) ister; ag hizi sinirlar */
        if(BR_VIDEO && BR_PAGE == 4 && BR_IW > 0 && TICKS - BR_LAST >= 5){
            BR_LAST = TICKS;
            br_act("a=frame");
        }
        if(BR_PAGE == 4 && BR_IW > 0){
            for(int r = 0; r < BR_IH; r++)
                for(int c = 0; c < BR_IW; c++){
                    u32 px = BR_IMG[r * BR_VW + c];
                    for(int dy = 0; dy < sc; dy++)
                        for(int dx = 0; dx < sc; dx++)
                            pset(vx + c * sc + dx, vy + r * sc + dy, px);
                }
        }
        if(BR_PAGE == 1){
            ds(wx + 40, wy + 115, "BAGLANILAMADI", CRD, 0, 1);
            ds(wx + 40, wy + 135, BR_ERR, CTXT, 0, 1);
        }
    }
    else {
        ds(wx + 40, wy + 115, "HAKKINDA", CW, 0, 1);
        ds(wx + 40, wy + 135, "WindOS 18.1", CTXT, 0, 1);
        ds(wx + 40, wy + 155, "Sayfalar host'taki Chromium ile acilir (koprü).", CGY, 0, 1);
        ds(wx + 40, wy + 171, "Ok tuslari ile sayfayi kaydirin.", CGY, 0, 1);
    }
}

/* ---- Spotlight: uygulama ve ayar aramasi ----
   '/' tusu (odak yokken) veya ust cubuktaki arama simgesi acar.
   Yazdikca eslesen kayitlar listelenir; Yukari/Asagi ile secilir,
   Enter ile acilir, ESC ile kapanir. Eslesme buyuk/kucuk harf duyarsizdir. */
typedef struct { const char *name; const char *kw; const char *tag; } SpEntry;
/* Sira onemli: sp_run() icindeki durum degerleriyle ayni sirada olmali */
static const SpEntry SP_APPS[] = {
    { "DOSYALAR",         "dosya file finder",             "UYGULAMA" },
    { "TERMINAL",         "terminal komut cmd shell",      "UYGULAMA" },
    { "MUZIK",            "muzik music ses melodi",        "UYGULAMA" },
    { "MIKU",             "miku kart card",                "UYGULAMA" },
    { "NOT DEFTERI",      "not notepad yazi",              "UYGULAMA" },
    { "YILAN OYUNU",      "yilan snake oyun game",         "UYGULAMA" },
    { "TARAYICI",         "tarayici browser web internet", "UYGULAMA" },
    { "GOREV YONETICISI", "gorev task manager",            "UYGULAMA" },
    { "WINDAI",           "ai asistan yapay zeka",         "UYGULAMA" },
    { "SISTEM AYARLARI",  "sistem ayar settings",          "AYAR" },
    { "EKRAN AYARLARI",   "ekran ayar display settings",   "AYAR" },
};
#define SP_N    ((int)(sizeof(SP_APPS) / sizeof(SP_APPS[0])))
#define SP_ROWS 6
static char SP_BUF[32];
static int  SP_LEN = 0, SP_SEL = 0;
/* Kurulu programlar (PROGRAMS klasoru): dizinde SP_N'den sonra gelir */
#define SP_INST_MAX 16
#define SP_TOTAL    (SP_N + SP_INST_MAX)
static char SP_INST[SP_INST_MAX][FAT_NAME_MAX + 1];
static int  SP_INST_N = 0;

static int sp_fold(int c){ return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
/* needle, hay icinde (buyuk/kucuk harf duyarsiz) geciyor mu */
static int sp_has(const char *hay, const char *needle){
    if(!*needle) return 1;
    for(const char *h = hay; *h; h++){
        const char *a = h, *n = needle;
        while(*n && *a && sp_fold((unsigned char)*a) == sp_fold((unsigned char)*n)){ a++; n++; }
        if(!*n) return 1;
    }
    return 0;
}
static const char *sp_name_of(int i){ return i < SP_N ? SP_APPS[i].name : SP_INST[i - SP_N]; }
static const char *sp_kw_of(int i){ return i < SP_N ? SP_APPS[i].kw : ""; }
static const char *sp_tag_of(int i){ return i < SP_N ? SP_APPS[i].tag : "KURULU"; }
static int sp_hit(int i){ return sp_has(sp_name_of(i), SP_BUF) || sp_has(sp_kw_of(i), SP_BUF); }

/* Eslesen kayit indekslerini out'a yazar, sayisini dondurur. Kurulu programlar once gelir */
static int sp_collect(int *out){
    int n = 0;
    for(int i = SP_N; i < SP_N + SP_INST_N; i++) if(sp_hit(i)) out[n++] = i;
    for(int i = 0; i < SP_N; i++) if(sp_hit(i)) out[n++] = i;
    return n;
}

/* PROGRAMS klasorundeki programlari arama listesine alir */
static void sp_inst_cb(void *ctx, const char *name, unsigned is_dir, unsigned size){
    int k = 0;
    (void)ctx; (void)size;
    if(is_dir || SP_INST_N >= SP_INST_MAX || !prog_name_ok(name)) return;
    while(name[k] && k < FAT_NAME_MAX){ SP_INST[SP_INST_N][k] = name[k]; k++; }
    SP_INST[SP_INST_N][k] = 0;
    SP_INST_N++;
}

static void sp_refresh(void){
    SP_INST_N = 0;
    if(mvol_ready()) fat_list(&MVOL, "PROGRAMS", sp_inst_cb, 0);
}

static void sp_run(int i){
    if(i >= SP_N){                       /* kurulu program: terminalde calisir */
        TERM_OPEN = 1; FOCUS = 1;
        prog_try(SP_INST[i - SP_N]);
        SPOTLIGHT_OPEN = 0;
        return;
    }
    switch(i){
        case 0:  FO = 1; break;
        case 1:  TERM_OPEN = 1; FOCUS = 1; break;
        case 2:  MUS_OPEN = 1; mus_start(0); break;
        case 3:  MIKU_OPEN = 1; break;
        case 4:  NOTE_OPEN = 1; FOCUS = 2; break;
        case 5:  snake_open(); break;
        case 6:  BR_OPEN = 1; FOCUS = 4; break;
        case 7:  TASKMGR_OPEN = 1; break;
        case 8:  AI_OPEN = 1; break;
        case 9:  SYS_OPEN = 1; break;
        case 10: DISP_OPEN = 1; break;
        default: break;
    }
    SPOTLIGHT_OPEN = 0;
}

static void sp_open(void){
    SPOTLIGHT_OPEN = 1; START_MENU_OPEN = 0;
    SP_LEN = 0; SP_BUF[0] = 0; SP_SEL = 0;
    sp_refresh();
}

static void sp_key(u8 c){
    int hits[SP_TOTAL];
    if(c == 27){ SPOTLIGHT_OPEN = 0; return; }
    if(c == KEY_UP){ if(SP_SEL > 0) SP_SEL--; return; }
    if(c == KEY_DOWN){ SP_SEL++; return; }           /* ust siniri cizimde kesilir */
    if(c == '\n'){
        int n = sp_collect(hits);
        if(n > 0){ if(SP_SEL >= n) SP_SEL = n - 1; sp_run(hits[SP_SEL]); }
        return;
    }
    if(c == 8){ if(SP_LEN > 0){ SP_BUF[--SP_LEN] = 0; SP_SEL = 0; } return; }
    if(c >= 32 && c < 127 && SP_LEN < 30){
        SP_BUF[SP_LEN++] = (char)c; SP_BUF[SP_LEN] = 0; SP_SEL = 0;
    }
}

static void SPOTLIGHT_SEARCH(void){
    if(!SPOTLIGHT_OPEN) return;
    int hits[SP_TOTAL];
    int n = sp_collect(hits);
    if(SP_SEL >= n) SP_SEL = (n > 0) ? n - 1 : 0;
    i32 sp_w = 600, sp_h = 60;
    i32 sp_x = ((i32)SW - sp_w) / 2;
    i32 sp_y = (i32)SH / 3;
    i32 rows = n < SP_ROWS ? n : SP_ROWS;

    rrg(sp_x, sp_y, sp_w, sp_h, 15, BG_BASE);
    rb(sp_x, sp_y, sp_w, sp_h, WIN_BLUE, 2);
    disc(sp_x + 30, sp_y + 30, 10, CGY);
    if(SP_LEN == 0){
        ds(sp_x + 60, sp_y + 25, "ARA: UYGULAMA VEYA AYAR", CGY, 0, 1);
    } else {
        ds(sp_x + 60, sp_y + 25, SP_BUF, CTXT, 0, 1);
        ds(sp_x + 60 + 8 * SP_LEN, sp_y + 25, "|", WIN_BLUE, 0, 1);
    }

    i32 py = sp_y + sp_h + 6;
    if(rows > 0){
        i32 ph = rows * 34 + 12;
        rrg(sp_x, py, sp_w, ph, 15, BG_BASE);
        rb(sp_x, py, sp_w, ph, PAN_BD, 1);
        for(i32 r = 0; r < rows; r++){
            i32 ry = py + 6 + r * 34;
            int sel = (r == SP_SEL);
            if(sel) rr(sp_x + 8, ry, sp_w - 16, 30, 8, WIN_BLUE);
            ds(sp_x + 24, ry + 11, sp_name_of(hits[r]), sel ? CW : CTXT, 0, 1);
            ds(sp_x + sp_w - 110, ry + 11, sp_tag_of(hits[r]), sel ? CW : CGY, 0, 1);
            if(CLK(sp_x + 8, ry, sp_w - 16, 30)){ sp_run(hits[r]); return; }
        }
    } else if(SP_LEN > 0){
        rrg(sp_x, py, sp_w, 40, 15, BG_BASE);
        ds(sp_x + 24, py + 14, "SONUC YOK", CGY, 0, 1);
    }
}

static void close_focused(void){
    if(FOCUS == 1) TERM_OPEN = 0;
    else if(FOCUS == 2) NOTE_OPEN = 0;
    else if(FOCUS == 3) SN_OPEN = 0;
    else if(FOCUS == 4) BR_OPEN = 0;
    FOCUS = 0;
}

/* Kare basina adimlar (tempo) */
static void apps_tick(void){ mus_tick(); snake_tick(); }

/* ---- Kurulum ekrani: kullanici adi sorulur (Enter ile devam) ---- */
static void setup_finish(void){
    if(SETUP_LEN > 0){
        for(int i = 0; i < SETUP_LEN; i++) USER_NAME[i] = SETUP_BUF[i];
        USER_NAME[SETUP_LEN] = 0;
    }
    app_str(PROMPT_STR, USER_NAME, "@WINDOS:~$ ");
    SETUP_DONE = 1;
    DIRTY = 1;
}

static void setup_key(u8 c){
    if(c == 8){ if(SETUP_LEN > 0){ SETUP_BUF[--SETUP_LEN] = 0; } return; }
    if(c == '\n' || c == '\r'){ setup_finish(); return; }
    if(c >= 32 && c < 127 && SETUP_LEN < 20){
        SETUP_BUF[SETUP_LEN++] = (char)c;
        SETUP_BUF[SETUP_LEN] = 0;
    }
}

static void SETUP_SCREEN(void){
    i32 cx = (i32)SW / 2, cy = (i32)SH / 2;
    fr(0, 0, (i32)SW, (i32)SH, BG_BASE);
    dsc(0, cy - 110, (i32)SW, "WINDOS KURULUM", CW, 0, 2);
    dsc(0, cy - 40, (i32)SW, "Kullanici adinizi girin:", CGY, 0, 1);
    rrg(cx - 180, cy, 360, 36, 8, PAN_BG);
    rb(cx - 180, cy, 360, 36, WIN_BLUE, 2);
    ds(cx - 168, cy + 14, SETUP_BUF, CTXT, 0, 1);
    ds(cx - 168 + 8 * SETUP_LEN, cy + 14, "|", WIN_BLUE, 0, 1);
    dsc(0, cy + 60, (i32)SW, "Enter: devam (bos birakirsan 'kullanici' kullanilir)", CGY, 0, 1);
}

/* Bir tus olayi: her tus yeniden cizim ister */
static void on_key(u8 c){
    if(!c) return;
    DIRTY = 1;
    if(AI_BOOT_PHASE < 2) return;          /* acilis sirasinda tuslar yok sayilir */
    if(!SETUP_DONE){ setup_key(c); return; }
    if(SPOTLIGHT_OPEN){ sp_key(c); return; }
    if(c == '/' && FOCUS == 0){ sp_open(); return; }
    if(c == 27){ close_focused(); return; }
    if(FOCUS == 1 && TERM_OPEN){ term_key(c); return; }
    if(FOCUS == 2 && NOTE_OPEN){ note_key(c); return; }
    if(FOCUS == 3 && SN_OPEN){ sn_key(c); return; }
    if(FOCUS == 4 && BR_OPEN){ br_key(c); return; }
}

/* WindOS logosu: mavi yuvarlak kare icinde 4 beyaz parca (x,y sol ust, s kenar) */
static void logo_mark(i32 x, i32 y, i32 s){
    rr(x, y, s, s, s / 4, WIN_BLUE);
    i32 g = s / 8; if(g < 1) g = 1;
    i32 q = (s - 3 * g) / 2;
    fr(x + g, y + g, q, q, CW);
    fr(x + 2 * g + q, y + g, q, q, CW);
    fr(x + g, y + 2 * g + q, q, q, CW);
    fr(x + 2 * g + q, y + 2 * g + q, q, q, CW);
}

/* Dock altindaki kisa isim, kutucugun ortasina gelir */
static void dock_label(i32 cx, i32 y, const char *s){
    i32 w = (i32)klen(s) * 8;
    ds(cx - w / 2, y, s, CGY, 0, 1);
}

static void DESKTOP(void){
    fr(0, 0, (i32)SW, (i32)SH, BG_BASE);

    /* UST CUBUK */
    frg(0, 0, (i32)SW, 25, PAN_BG);
    logo_mark(12, 5, 16);
    ds(34, 8, "WINDOS", CTXT, 0, 1);
    /* Arama simgesi: Spotlight'i acar/kapatir */
    disc((i32)SW - 212, 10, 5, CW);
    disc((i32)SW - 212, 10, 3, PAN_BG);
    fr((i32)SW - 208, 14, 4, 2, CW);
    if(CLK((i32)SW - 222, 2, 24, 21)){ if(SPOTLIGHT_OPEN) SPOTLIGHT_OPEN = 0; else sp_open(); }

    /* DOCK */
    i32 dock_w = 725, dock_x = ((i32)SW - dock_w) / 2, dock_y = (i32)SH - 80;
    rrg(dock_x, dock_y, dock_w, 80, 15, DOCK_BG);
    rb(dock_x, dock_y, dock_w, 80, PAN_BD, 1);

    rr(dock_x + 15, dock_y + 10, 45, 45, 10, WIN_BLUE);
    fr(dock_x + 25, dock_y + 20, 10, 10, CW); fr(dock_x + 40, dock_y + 20, 10, 10, CW);
    fr(dock_x + 25, dock_y + 35, 10, 10, CW); fr(dock_x + 40, dock_y + 35, 10, 10, CW);
    if(CLK(dock_x + 15, dock_y + 10, 45, 45)) START_MENU_OPEN = !START_MENU_OPEN;

    rr(dock_x + 80, dock_y + 10, 45, 45, 10, COR);
    if(CLK(dock_x + 80, dock_y + 10, 45, 45)) FO = 1;

    rr(dock_x + 145, dock_y + 10, 45, 45, 10, LIN_ORG);
    if(CLK(dock_x + 145, dock_y + 10, 45, 45)){ TERM_OPEN = 1; FOCUS = 1; }

    rr(dock_x + 210, dock_y + 10, 45, 45, 10, AI_PURP);
    if(CLK(dock_x + 210, dock_y + 10, 45, 45)) AI_OPEN = 1;

    rr(dock_x + 275, dock_y + 10, 45, 45, 10, CGY);
    if(CLK(dock_x + 275, dock_y + 10, 45, 45)) SYS_OPEN = 1;

    rr(dock_x + 340, dock_y + 10, 45, 45, 10, CRD);
    if(CLK(dock_x + 340, dock_y + 10, 45, 45)) TASKMGR_OPEN = 1;

    rr(dock_x + 405, dock_y + 10, 45, 45, 10, CHR_GRN);
    if(CLK(dock_x + 405, dock_y + 10, 45, 45)) MUS_OPEN = 1;
    rr(dock_x + 470, dock_y + 10, 45, 45, 10, 0xFF39C5BBu);
    if(CLK(dock_x + 470, dock_y + 10, 45, 45)) MIKU_OPEN = 1;
    rr(dock_x + 535, dock_y + 10, 45, 45, 10, MAC_YEL);
    if(CLK(dock_x + 535, dock_y + 10, 45, 45)){ NOTE_OPEN = 1; FOCUS = 2; }
    rr(dock_x + 600, dock_y + 10, 45, 45, 10, CGN);
    if(CLK(dock_x + 600, dock_y + 10, 45, 45)) snake_open();
    rr(dock_x + 665, dock_y + 10, 45, 45, 10, 0xFF4285F4u);
    if(CLK(dock_x + 665, dock_y + 10, 45, 45)){ BR_OPEN = 1; FOCUS = 4; }

    /* Kutucuk isimleri (her kutucugun altinda) */
    static const i32 DOCK_X[11] = {15, 80, 145, 210, 275, 340, 405, 470, 535, 600, 665};
    static const char *DOCK_NM[11] = {"BASLA", "DOSYA", "TERM", "WINDAI", "AYAR", "GOREV", "MUZIK", "MIKU", "NOT", "YILAN", "WEB"};
    for(int di = 0; di < 11; di++) dock_label(dock_x + DOCK_X[di] + 22, dock_y + 60, DOCK_NM[di]);

    FILEMGR(); TERMINAL_APP(); TASKMGR_APP(); SYSTEM_APP(); DISPLAY_APP(); WINDAI_ASSISTANT();
    MUSIC_APP(); MIKU_APP(); NOTES_APP(); SNAKE_APP(); BROWSER_APP();
    START_MENU(); SPOTLIGHT_SEARCH();

}

/* Acilis: AI tarama ekrani */
static void BOOT_SCREEN(void){
    fr(0, 0, (i32)SW, (i32)SH, CK);
    logo_mark(((i32)SW - 72) / 2, (i32)SH / 2 - 130, 72);
    dsc(0, (i32)SH / 2 - 8, (i32)SW, "WINDOS", CW, CK, 2);
    dsc(0, (i32)SH / 2 + 30, (i32)SW, "Yukleniyor...", CGY, CK, 1);
    AI_TIMER++;
    if(AI_TIMER >= 90){ AI_BOOT_PHASE = 2; DIRTY = 1; }   /* masaustu bu karede cizilsin */
}

/* ========================================================================= */
/* ZAMANLAYICI: PIC yeniden eslestirme, IDT, PIT IRQ0                        */
/* ========================================================================= */
/* Ana dongu her karede bir kez calisir; kareler arasinda islemci hlt ile uyur.
   Klavye ve fare hala yoklama (polling) ile okunur; yalnizca IRQ0 aciktir. */

struct __attribute__((packed)) IdtEntry { u16 off_lo; u16 sel; u8 zero; u8 type; u16 off_hi; };
struct __attribute__((packed)) IdtPtr  { u16 limit; u32 base; };
static struct IdtEntry IDT[256];

__attribute__((interrupt, target("general-regs-only")))
static void timer_isr(void *frame){
    (void)frame;
    TICKS++;
    outb(0x20, 0x20);                       /* EOI: ana PIC */
}

static void idt_gate(u8 n, u32 handler, u16 cs){
    IDT[n].off_lo = (u16)(handler & 0xFFFF);
    IDT[n].sel    = cs;                     /* GRUB'un 32-bit kod segmenti (calisirken okunur) */
    IDT[n].zero   = 0;
    IDT[n].type   = 0x8E;                   /* mevcut, DPL 0, 32-bit kesme kapisi */
    IDT[n].off_hi = (u16)((handler >> 16) & 0xFFFF);
}

/* PIC: IRQ0 -> 0x20..0x27, IRQ8 -> 0x28..; yalnizca IRQ0 acik */
static void pic_setup(void){
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFE); outb(0xA1, 0xFF);
}

/* PIT kanal 0: kare dalgasi, verilen Hz */
static void pit_init(u32 hz){
    u32 div = 1193182u / hz;
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
}

static void timer_init(void){
    u16 cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    idt_gate(0x20, (u32)(unsigned long)timer_isr, cs);
    struct IdtPtr p;
    p.limit = (u16)(sizeof(IDT) - 1);
    p.base  = (u32)(unsigned long)IDT;
    __asm__ volatile("lidt %0" :: "m"(p));
    pic_setup();
    pit_init(FRAME_HZ);
    __asm__ volatile("sti");
}

/* Bir sonraki zamanlayici tick'ine kadar uyur */
static void frame_wait(void){
    u32 t = TICKS;
    while(TICKS == t) __asm__ volatile("hlt");
}

/* Kare animasyon gerektiriyor mu (ses, oyun, saat, acilis, kurulum) */
static int animating(void){
    return AI_BOOT_PHASE < 2 || MUS_PLAYING || (SN_OPEN && !SN_DEAD) || MIKU_OPEN
        || TASKMGR_OPEN || (BR_OPEN && BR_PAGE == 4 && BR_VIDEO);
}

/* ========================================================================= */
/* GIRIS                                                                     */
/* ========================================================================= */
void kernel_main(multiboot_info_t *mbi){
    u8 bpp_bits = mbi->framebuffer_bpp;
    if(bpp_bits == 0) bpp_bits = 32;
    BPP = bpp_bits / 8;

    FB = (volatile u8*)(unsigned long)mbi->framebuffer_addr;
    SW = mbi->framebuffer_width;
    SH = mbi->framebuffer_height;
    PITCH = mbi->framebuffer_pitch;
    if(PITCH == 0) PITCH = SW * BPP;

    /* Lineer grafik modu yoksa metin modunda hata goster */
    if(!(mbi->flags & (1u << 12)) || mbi->framebuffer_type != 1 || !FB || SW == 0 || (BPP != 2 && BPP != 3 && BPP != 4)){
        volatile u16 *vga = (volatile u16*)0xB8000;
        const char *msg = "WINDOS: LINEER GRAFIK MODU ALINAMADI (gfxpayload kontrol et)";
        for(u32 i = 0; i < 80 * 25; i++) vga[i] = 0x0720;
        for(u32 i = 0; msg[i] && i < 80; i++) vga[i] = (u16)(0x0C00 | (u8)msg[i]);
        while(1) __asm__ volatile("cli; hlt");
    }
    if(SW > MAX_SW) SW = MAX_SW;
    if(SH > MAX_SH) SH = MAX_SH;

    u32 flags = *((u32*)((u8*)mbi + 0));
    if(flags & 1){ u32 mem_upper_kb = *((u32*)((u8*)mbi + 8)); SYS_RAM_MB = (mem_upper_kb / 1024) + 1; }
    else SYS_RAM_MB = 0;                    /* bilinmiyor */

    /* GRUB modulleri: flags bit 3 (mods) -> ilk modul disk imaji */
    {
        u32 mflags = *((u32*)((u8*)mbi + 0));
        u32 mcount = (mflags & 8) ? *((u32*)((u8*)mbi + 20)) : 0;
        u32 maddr  = (mflags & 8) ? *((u32*)((u8*)mbi + 24)) : 0;
        if(mcount > 0){
            u32 mstart = *((u32*)(unsigned long)maddr);
            u32 mend   = *((u32*)(unsigned long)(maddr + 4));
            if(mend > mstart){ RD_BASE = (const unsigned char *)(unsigned long)mstart; RD_LEN = mend - mstart; }
        }
    }
    mouse_init(); term_init();
    nic_init();
    ac97_init();
    timer_init();

    while(1){
        frame_wait();
        ps2_pump();
        mouse_poll();
        nic_poll();
        ac97_tick();
        for(int ki = 0; ki < 8; ki++) on_key(kbd_poll());
        apps_tick();
        int anim = animating();
        /* Animasyon yeni bittiyse (ornek: muzik bitti) son durum bir kez cizilir */
        if(DIRTY || anim || ANIM_PREV){
            DIRTY = 0;
            if(AI_BOOT_PHASE < 2){ BOOT_SCREEN(); }
            else if(!SETUP_DONE){ SETUP_SCREEN(); }
            else { DESKTOP(); CUR(); }
            swap_buffers();
        }
        ANIM_PREV = anim;
        /* Tiklama kenari bu karede tuketildi */
        PMLB = MLB;
    }
}
