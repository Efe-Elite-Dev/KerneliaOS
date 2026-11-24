/* KerneliaOS ornek .wind programi: ekranda yazi gosterir, bir tusa basinca cikar.
   Konuma bagimsiz olmali: global veri, dize sabiti ve mutlak adres kullanmaz;
   yalnizca api tablosunu ve yigini kullanir. Derleme: app/samples/build_samples.py */
#include "../wind.h"

__attribute__((section(".text.entry")))
void entry(const wind_api *api){
    char a[12];
    char b[8];
    a[0] = 'M'; a[1] = 'E'; a[2] = 'R'; a[3] = 'H'; a[4] = 'A';
    a[5] = 'B'; a[6] = 'A'; a[7] = ' '; a[8] = 'W'; a[9] = 'I'; a[10] = 'N'; a[11] = 0;
    b[0] = 'T'; b[1] = 'U'; b[2] = 'S'; b[3] = ' '; b[4] = 'C'; b[5] = 'I'; b[6] = 'K'; b[7] = 0;

    api->fill(0, 0, 320, 120, 0x00202830u);
    api->text(16, 16, a, 0x00FFFFFFu, 2);
    api->text(16, 60, b, 0x0080C0FFu, 1);
    api->present();
    while(!api->key()) api->wait();
}
