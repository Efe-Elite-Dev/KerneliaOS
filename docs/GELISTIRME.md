# KerneliaOS geliştirme rehberi

Bu belge çekirdeği nasıl geliştireceğimizi anlatır. Her değişiklik bu adımlardan geçmeli.

## 1. Derle ve test et
- `make` → `kernel.bin`, `make iso` → `kerneliaos.iso`.
- `make test` → QEMU senaryoları (`tools/qemu_test.sh`) ve host testleri (`test_mem`, `test_dns`).
- Saf mantık (ayırıcı, DNS, FAT) için host testi yaz. Donanım davranışı için `WIND_TEST` ve QEMU testi yaz.

## 2. Hata ayıkla
- Seri çıktı: `-serial stdio` ya da `build/.../serial.log`. Panik dökümü de buraya yazılır.
- gdb: `make debug` QEMU'yu duraklatır; ayrı terminalde `gdb kernel.bin -ex 'target remote :1234'`.

## 3. Modül sınırları
- Bugün tüm `app/*.c` dosyaları `kernel.c` içine `#include` ile girer (tek derleme birimi). Bu yüzden her şey `static`.
- Bir sonraki adım: alt sistemleri (bellek, zamanlama, sürücüler) ayrı başlık dosyalarıyla tanımlamak. Bunun için iki yol var (aşağıda).

## 4. Kesme ve zamanlama değişiklikleri
- Bu alanlar en çok hata çıkarır. Değişiklik sonrası panik ve zamanlayıcı testlerini çalıştır.

## 5. Sürücüler
- Klavye, fare, disk ve ağ için ortak bir arayüz tanımla; yeni donanım bu arayüze eklensin.

## 6. Belgeleme
- Her alt sistem için kısa not: ne yapıyor, sınırları neler.

## 7. Sıra
- Önce kararlılık (testler yeşil, CI yeşil), sonra özellik.
