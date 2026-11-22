/* keymap_tr.h icin host testi: tuslarin dogru karakteri verdigini ve kurali kontrol eder */
#include <stdio.h>
#include "../keymap_tr.h"

static int fails = 0;
static void eq(const char *name, unsigned got, unsigned want){
    if(got != want){ printf("FAIL %s: got 0x%02X want 0x%02X\n", name, got, want); fails++; }
    else printf("ok   %s\n", name);
}

/* kernel.c kbd_poll kuralinin kopyasi: harf tuslari CapsLock ile de buyur */
static unsigned pick(unsigned sc, int shift, int caps){
    unsigned lo = TR_KEYS[sc][0], up = TR_KEYS[sc][1];
    if(!lo) return 0;
    int letter = (lo >= 'a' && lo <= 'z') || (lo >= 0x80 && lo <= 0x8B);
    return (letter ? (shift ^ caps) : shift) ? up : lo;
}

int main(void){
    eq("g tusu (0x22) normal",          pick(0x22, 0, 0), 'g');
    eq("g tusu shift",                  pick(0x22, 1, 0), 'G');
    eq("g tusu caps",                   pick(0x22, 0, 1), 'G');
    eq("[ tusu = g-breve (0x82)",       pick(0x1A, 0, 0), 0x82);
    eq("] tusu = u-umlaut (0x88)",      pick(0x1B, 0, 0), 0x88);
    eq("; tusu = s-cedilla (0x86)",     pick(0x27, 0, 0), 0x86);
    eq("' tusu normal = i",             pick(0x28, 0, 0), 'i');
    eq("' tusu shift = I-dot (0x8B)",   pick(0x28, 1, 0), 0x8B);
    eq("I tusu normal = dotless i",     pick(0x17, 0, 0), 0x8A);
    eq("I tusu shift = I",              pick(0x17, 1, 0), 'I');
    eq(", tusu = o-umlaut (0x84)",      pick(0x33, 0, 0), 0x84);
    eq(". tusu = c-cedilla (0x80)",     pick(0x34, 0, 0), 0x80);
    eq("sayi 2 shift = '",              pick(0x03, 1, 0), '\'');
    eq("sayi 0 shift = =",              pick(0x0B, 1, 0), '=');
    eq("- tusu = *",                    pick(0x0C, 0, 0), '*');
    eq("bosluk",                        pick(0x39, 0, 0), ' ');
    eq("bos tus yok",                   pick(0x3B, 0, 0), 0);
    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
