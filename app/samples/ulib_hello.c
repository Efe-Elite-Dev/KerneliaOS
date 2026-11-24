/* ULIB ornegi: mini libc (app/ulib) ile yazildi. Ring 3'te calisir; sistem cagrilari int 0x80. */
#include "../ulib/ulib.h"

void _start(void){
    ul_printf("MERHABA ULIB\r\n");
    ul_printf("ZAMAN (TICK): %u\r\n", ul_ticks());
    char *p = malloc(64);
    if(p){
        strcpy(p, "HEAP CALISIYOR");
        ul_printf("%s, %d, %x, %c %%\r\n", p, -42, 0xBEEF, 'Z');
        free(p);
    }
    ul_exit(7);
}
