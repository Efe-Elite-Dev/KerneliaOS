/* Koruma testi: kullanici programi cekirdek belleğine yazar. Beklenen: program sonlanir
   (PROGRAM COKTU mesaji), cekirdek calismaya devam eder. */
#include "../ulib/ulib.h"

void _start(void){
    ul_printf("CEKIRDEK BELLEGINE YAZILIYOR\r\n");
    volatile unsigned int *kernel_mem = (volatile unsigned int *)0x00100000u;
    *kernel_mem = 1;
    ul_printf("BURAYA GELINMEMELI\r\n");
    ul_exit(0);
}
