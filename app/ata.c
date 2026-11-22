/* WindOS ATA PIO surucusu. Bkz. ata.h. UYARI: donanimda henuz test edilmedi. */
#include "ata.h"

#define ATA_DATA 0x1F0
#define ATA_SECC 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_DRVH 0x1F6
#define ATA_CMD  0x1F7   /* okuma: durum (ayni adres) */

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80

static inline unsigned char inb(unsigned short port){
    unsigned char r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outb(unsigned short port, unsigned char v){
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
/* 16 bitlik sozcukleri porttan buf'a oku (n sozcuk) */
static inline void insw(unsigned short port, unsigned char *buf, unsigned n){
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(n) : "d"(port) : "memory");
}

/* 400 ns bekleme: durum portunu dort kez oku */
static void ata_delay(void){ for(int i = 0; i < 4; i++) (void)inb(ATA_CMD); }

static int ata_wait_ready(void){
    for(unsigned t = 0; t < 1000000u; t++){
        if(!(inb(ATA_CMD) & ST_BSY)) return 0;
    }
    return -1;
}

static int ata_read_one(unsigned lba, unsigned char *buf){
    if(ata_wait_ready() != 0) return -1;
    outb(ATA_DRVH, (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));   /* ana surucu, LBA */
    outb(ATA_SECC, 1);
    outb(ATA_LBA0, (unsigned char)lba);
    outb(ATA_LBA1, (unsigned char)(lba >> 8));
    outb(ATA_LBA2, (unsigned char)(lba >> 16));
    outb(ATA_CMD, 0x20);                                              /* READ SECTORS */
    ata_delay();
    for(unsigned t = 0; ; t++){
        unsigned char s = inb(ATA_CMD);
        if(s & (ST_ERR | ST_DF)) return -1;
        if(!(s & ST_BSY) && (s & ST_DRQ)) break;
        if(t > 1000000u) return -1;
    }
    insw(ATA_DATA, buf, 256);   /* 256 sozcuk = 512 bayt */
    ata_delay();
    return 0;
}

int ata_read(void *ctx, unsigned lba, unsigned count, unsigned char *buf){
    (void)ctx;
    for(unsigned i = 0; i < count; i++){
        if(ata_read_one(lba + i, buf + i * 512u) != 0) return -1;
    }
    return 0;
}
