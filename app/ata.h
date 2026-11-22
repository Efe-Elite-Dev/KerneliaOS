/* WindOS ATA PIO surucusu (birincil kanal, ana disk, 28-bit LBA: okuma ve yazma).
   Donanim portlari gerektirir: QEMU'da -hda <imaj> ile calisir.
   Arayuzler fat_read_fn ve fat_write_fn ile uyumludur:
     ata_read(ctx, lba, count, buf), ata_write(ctx, lba, count, buf). */
#ifndef ATA_H
#define ATA_H

/* lba'dan count sektor oku. Donus: 0 basarili, -1 hata (zaman asimi ya da ERR) */
int ata_read(void *ctx, unsigned lba, unsigned count, unsigned char *buf);

/* lba'dan count sektor yaz, sonra disk onbellegini bosalt (FLUSH CACHE).
   Donus: 0 basarili, -1 hata (zaman asimi, ERR ya da DF) */
int ata_write(void *ctx, unsigned lba, unsigned count, const unsigned char *buf);

#endif
