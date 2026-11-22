/* WindOS PE ornegi: kernel32'den yalnizca GetStdHandle, WriteFile ve ExitProcess kullanir.
   CRT yok; giris noktasi _start (i386 MinGW'de sembol __start). Derleme: app/samples/build_pe.py (i686-w64-mingw32-gcc).
   Kaynak Windows'ta da derlenebilir ama WindOS yalnizca bu API'leri saglar. */
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long nStdHandle);
__declspec(dllimport) int __stdcall WriteFile(void *h, const void *buf, unsigned long n,
                                              unsigned long *written, void *ov);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int code);

static const char msg[] = "MERHABA EXE\r\nBU PROGRAM PE32 OLARAK YUKLENDI\r\n";

void _start(void){
    unsigned long written = 0;
    void *out = GetStdHandle((unsigned long)-11);   /* STD_OUTPUT_HANDLE */
    WriteFile(out, msg, sizeof msg - 1, &written, 0);
    ExitProcess(0);
}
