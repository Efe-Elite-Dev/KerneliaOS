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
static const u8 F8[128][8]={
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
 ['|']={0x18,0x18,0x18,0,0x18,0x18,0x18,0},['}']={0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0},['~']={0x6E,0x3B,0,0,0,0,0,0},['_']={0,0,0,0,0,0,0,0x7F}
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
    if((u8)ch >= 128) ch = '?';
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
static const char SCMAP[128] = { 0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,'\t','Q','W','E','R','T','Y','U','I','O','P','[',']','\n',0,'A','S','D','F','G','H','J','K','L',';','\'','`',0,'\\','Z','X','C','V','B','N','M',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0 };
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

#define KEY_UP    0x81
#define KEY_DOWN  0x82
#define KEY_LEFT  0x83
#define KEY_RIGHT 0x84

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
    char c = SCMAP[sc]; if(!c) return 0;
    if(c >= 'A' && c <= 'Z' && !(K_SH ^ K_CP)) c = (char)(c + 32);
    return (u8)c;
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

static void FILEMGR(void){
    if(!FO) return;
    i32 fw = 700, fh = 450, fx = FX, fy = FY;
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
    dsc(fx, fy + fh / 2 - 16, fw, "DEPOLAMA SURUCUSU YOK", CGY, 0, 1);
    dsc(fx, fy + fh / 2 + 6, fw, "Dosyalar okunamiyor.", CGY, 0, 1);
}

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

static void ip_rx(const u8 *p, int n){
    if(n < 20 || (p[0] >> 4) != 4) return;
    int ihl = (p[0] & 0x0F) * 4;
    int tot = get16(p + 2);
    if(ihl < 20 || tot < ihl || tot > n) return;
    if(get32(p + 16) != NET_IP) return;
    if(p[9] == 1) icmp_rx(get32(p + 12), p + ihl, tot - ihl);
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

    if(str_eq(cmd, "help")){ tl_add("KOMUTLAR: help ver mem echo clear time muzik miku not snake web exit lspci"); tl_add("AG: ping [ip]  (ornek: ping 10.0.2.2)"); tl_add("SES: ses [hz]  (ornek: ses 440)"); }
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

/* ---- TARAYICI: ag surucusu yok, yalnizca yerlesik sayfalar ---- */
static char BR_URL[64]; static int BR_ULEN = 0;
static char BR_TARGET[64];
static int BR_PAGE = 0; /* 0 ana sayfa, 1 ag yok, 2 hakkinda */

static void br_go(void){
    BR_URL[BR_ULEN] = 0;
    if(BR_ULEN == 0 || str_eq(BR_URL, "home")) BR_PAGE = 0;
    else if(str_eq(BR_URL, "about")) BR_PAGE = 2;
    else { BR_PAGE = 1; kcpy(BR_TARGET, BR_URL); }
}

static void br_key(u8 c){
    if(c == 8){ if(BR_ULEN > 0) BR_ULEN--; return; }
    if(c == '\n' || c == '\r'){ br_go(); return; }
    if(c >= 32 && c < 127 && BR_ULEN < 60) BR_URL[BR_ULEN++] = (char)c;
}

static void BROWSER_APP(void){
    if(!BR_OPEN) return;
    const i32 wx = 200, wy = 140, ww = 560, wh = 380;
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
        ds(wx + 40, wy + 135, "Ag surucusu yok: internet sayfalari acilamaz.", CGY, 0, 1);
        ds(wx + 40, wy + 155, "Adres kutusuna 'about' yazip Enter'a basin.", CGY, 0, 1);
    }
    else if(BR_PAGE == 1){
        ds(wx + 40, wy + 115, "BAGLANILAMIYOR", CRD, 0, 1);
        ds(wx + 40, wy + 135, "Ag surucusu yok (NIC, TCP/IP, DNS).", CTXT, 0, 1);
        ds(wx + 40, wy + 155, "Adres:", CGY, 0, 1);
        ds(wx + 100, wy + 155, BR_TARGET, CW, 0, 1);
        ds(wx + 40, wy + 185, "Ana sayfa icin adres kutusuna 'home' yazin.", CGY, 0, 1);
    }
    else {
        ds(wx + 40, wy + 115, "HAKKINDA", CW, 0, 1);
        ds(wx + 40, wy + 135, "WindOS 18.1", CTXT, 0, 1);
        ds(wx + 40, wy + 155, "Tarayici yerlesik sayfalari gosterir.", CGY, 0, 1);
        ds(wx + 40, wy + 171, "Internet icin ag surucusu gerekir.", CGY, 0, 1);
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
/* Eslesen kayit indekslerini out'a yazar, sayisini dondurur */
static int sp_collect(int *out){
    int n = 0;
    for(int i = 0; i < SP_N; i++)
        if(sp_has(SP_APPS[i].name, SP_BUF) || sp_has(SP_APPS[i].kw, SP_BUF)) out[n++] = i;
    return n;
}

static void sp_run(int i){
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
}

static void sp_key(u8 c){
    int hits[SP_N];
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
    int hits[SP_N];
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
            const SpEntry *e = &SP_APPS[hits[r]];
            i32 ry = py + 6 + r * 34;
            int sel = (r == SP_SEL);
            if(sel) rr(sp_x + 8, ry, sp_w - 16, 30, 8, WIN_BLUE);
            ds(sp_x + 24, ry + 11, e->name, sel ? CW : CTXT, 0, 1);
            ds(sp_x + sp_w - 110, ry + 11, e->tag, sel ? CW : CGY, 0, 1);
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

static void DESKTOP(void){
    fr(0, 0, (i32)SW, (i32)SH, BG_BASE);

    /* UST CUBUK */
    frg(0, 0, (i32)SW, 25, PAN_BG);
    ds(15, 8, "WINDOS", CTXT, 0, 1);
    /* Arama simgesi: Spotlight'i acar/kapatir */
    disc((i32)SW - 212, 10, 5, CW);
    disc((i32)SW - 212, 10, 3, PAN_BG);
    fr((i32)SW - 208, 14, 4, 2, CW);
    if(CLK((i32)SW - 222, 2, 24, 21)){ if(SPOTLIGHT_OPEN) SPOTLIGHT_OPEN = 0; else sp_open(); }

    /* DOCK */
    i32 dock_w = 725, dock_x = ((i32)SW - dock_w) / 2, dock_y = (i32)SH - 80;
    rrg(dock_x, dock_y, dock_w, 65, 15, DOCK_BG);
    rb(dock_x, dock_y, dock_w, 65, PAN_BD, 1);

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

    FILEMGR(); TERMINAL_APP(); TASKMGR_APP(); SYSTEM_APP(); DISPLAY_APP(); WINDAI_ASSISTANT();
    MUSIC_APP(); MIKU_APP(); NOTES_APP(); SNAKE_APP(); BROWSER_APP();
    START_MENU(); SPOTLIGHT_SEARCH();

}

/* Acilis: AI tarama ekrani */
static void BOOT_SCREEN(void){
    fr(0, 0, (i32)SW, (i32)SH, CK);
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
        || TASKMGR_OPEN;
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
