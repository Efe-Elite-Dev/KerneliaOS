# KerneliaOS Tarayıcı (app/)

KerneliaOS'un kendi tarayıcısı. Hedef: önce basit HTML sayfalarını KerneliaOS içinde göstermek, sonra adım adım daha modern web'e yaklaşmak.

## Aşamalar

1. **HTML ayrıştırıcı** (`html_lite.c/.h`) — bitti. Etiketleri, başlıkları, bağlantıları, satır kesmelerini ve HTML karakter kodlarını çözer. Script/style/head içeriğini atlar. libc gerektirmez; kernel bayraklarıyla derlenir.
2. Sayfa yerleşimi ve çizim — metni pencerede satır satır yerleştirme, başlık boyutları, bağlantı rengi. (Henüz başlamadı; şu anki TARAYICI penceresi kernel.c içinde.)
3. Tıklanabilir bağlantılar ve geçmiş.
4. HTTPS (TLS) — ayrı ve büyük bir iş.
5. Basit JavaScript yorumlayıcı.
6. Modern siteler (YouTube dahil) — bu aşamaya gelindiğinde bile tam motor gerekir; bkz. ana konuşma.

## Test

Host üzerinde (gcc gerekir):

```
gcc -std=c99 -Wall -Wextra -Werror -O2 app/html_lite.c app/test/test_html_lite.c -o test_html_lite
./test_html_lite
```

Kernel bayraklarıyla derleme kontrolü:

```
gcc -m32 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall -Wextra -Werror -c app/html_lite.c -o html_lite.o
```

## Ayrıştırıcının çıktısı

`hl_parse` bir metin tamponu, bir adres (href) tamponu ve bir parça (run) dizisi doldurur. Her parça: metin aralığı, stil (normal / 1..6 başlık / bağlantı), önce gelmesi gereken kesme (satır ya da paragraf) ve varsa bağlantı adresi taşır.
