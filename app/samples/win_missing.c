/* Uyum katmani negatif testi: desteklenmeyen bir API (GetFileAttributesA).
   Beklenen: program yuklenmez, ekranda "EKSIK API: kernel32.dll!GetFileAttributesA" yazar. */
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long nStdHandle);
__declspec(dllimport) unsigned long __stdcall GetFileAttributesA(const char *name);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int code);

void _start(void){
    if(GetFileAttributesA("X") == 0) ExitProcess(2);
    ExitProcess(0);
}
