/* WindOS Win32 uyum katmani v0 testi: yalnizca desteklenen konsol API'lerini kullanir.
   WriteConsoleA, GetLastError, GetStdHandle, ExitProcess. CRT yok. Derleme: app/samples/build_user.py */
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long nStdHandle);
__declspec(dllimport) int __stdcall WriteConsoleA(void *h, const void *buf, unsigned long n,
                                                  unsigned long *written, void *reserved);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int code);

static const char msg[] = "WIN32 KONSOL TESTI\r\nWriteConsoleA CALISTI\r\n";

void _start(void){
    unsigned long written = 0;
    void *out = GetStdHandle((unsigned long)-11);
    WriteConsoleA(out, msg, sizeof msg - 1, &written, 0);
    ExitProcess(GetLastError() == 0 ? 0 : 1);
}
