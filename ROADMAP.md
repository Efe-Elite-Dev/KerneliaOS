# WindOS Yol Haritasi

Bu dosya, WindOS'un bir sonraki adimlarini sirasiyla anlatir. Her adim bir oncekinin ustune kurulur.
Hedef mimari: once 32-bit x86 (mevcut), sonra 64-bit (x86-64). Uygulamalar icin kendi mini libc'miz olacak.

## Asama 1: Bellek yonetimi

- Sayfalama (paging) acmak: once cekirdegi birebir (identity) eslemek, sonra her uygulamaya ayri sayfa tablosu vermek.
- Fiziksel sayfa ayiricisi (frame allocator) yazmak.
- `kmalloc` / `kfree` (cekirdek heap) yazmak.
- Tek adres alani sorununun ilk buyuk adimi.

## Asama 2: Zamanlayici ve cok gorevlilik

- PIC'i yeniden programlayip PIT (IRQ0) kesmesini acmak.
- Her kesmede baglam degistiren basit bir round-robin zamanlayici yazmak.
- Ornek: arka planda indirme, on planda terminal ayni anda calisabilsin.

## Asama 3: Kullanici modu ve sistem cagrilari

- Programlari ring 3'te calistirmak.
- Cagrilari `int 0x80` ile bir sistem cagri tablosuna yonlendirmek.
- Mevcut `kernel32` benzeri cagrilari bu tablonun arkasina almak.
- Bir programin cekirdegi cokertme riskini azaltmak.

## Asama 4: Mini libc

- `memcpy`, `memset`, `strlen`, `strcmp`, `malloc`, `free`, `printf` gibi temel fonksiyonlari kendi kutuphanemizde toplamak.
- Sistem cagrilari uzerine kurulacak; hem bizim programlarimiz hem de ileride tasinacak kod bunu kullanacak.
- Not: Onceki surumde "libc'siz" tercih edilmisti. Bu asamadan itibaren kendi libc'miz olacak; ama bu, standart bir C kutuphanesinin tamamen kendi yazimizla degistirilmesi anlamina gelir.

## Asama 5: Ag

- Mevcut ICMP ve IP isleme uzerine TCP eklemek.
- Sonra DNS ve basit bir HTTP istemcisi.
- Tarayici fikrinin onkosuludur.

## Asama 6: Test ve kararlilik

- QEMU'da otomatik testler: acilis, dosya kurma, ag, sistem cagrilari.
- Cekirdek paniginda hem ekrana hem dosyaya kayit yazan bir mekanizma.

## Asama 7: Uzun vadeli

- 64-bit (long mode, x86-64) desteği.
- UEFI onyukleme.
- Gercek donanim surucüleri (GPU, ses, USB, Wi-Fi).
- Bunlar projenin en buyuk islerdir ve sonraya birakilmistir.

## Tarayici

- Tarayici, Asama 5 tamamlandiktan sonra baslar.
- Once kucuk bir HTML/metin tarayici. Chromium'un tasinmasi uzun vadeli bir hedeftir, ama bu asamanin kapsaminda degildir.
