/* WindOS ATA PIO surucusu (birincil kanal, ana disk, 28-bit LBA okuma).
   Donanim portlari gerektirir: QEMU'da -hda <imaj> ile calisir.
   Arayuz fat_read_fn ile uyumludur: ata_read(ctx, lba, count, buf). */
#ifndef ATA_H
#define ATA_H

/* lba'dan count sektor oku. Donus: 0 basarili, -1 hata (zaman asimi ya da ERR) */
int ata_read(void *ctx, unsigned lba, unsigned count, unsigned char *buf);

#endif
