/* KerneliaOS Turkce Q klavye: tarama kodu (PS/2 set 1) -> {normal, shift} karakter.

   Harf tuslari icin ikinci sutun buyuk harftir; Shift ve CapsLock birlikte
   harf buyuklugunu belirler. Noktalama tuslari icin ikinci sutun Shift'li hal.

   Turkce harfler, kernel'in yazi tipindeki 0x80-0x8B kodlarina baglanir:
     0x80 c  0x81 C(c-cedilla)   0x82 g  0x83 G(breve)
     0x84 o  0x85 O              0x86 s  0x87 S
     0x88 u  0x89 U              0x8A i-without-dot (ı)  0x8B I-with-dot (İ)

   DOGRULANMADI: sol ustteki " tusu (0x29), \ tusu (0x2B), / tusu (0x35) ve
   Shift'li sayi satiri sembollerinin Turkce Q ile birebir ayni olup olmadigi.
   Bunlar gercek bilgisayarda denenerek kontrol edilmeli. */
#ifndef KEYMAP_TR_H
#define KEYMAP_TR_H

static const unsigned char TR_KEYS[128][2] = {
    [0x01] = {27, 27},
    [0x02] = {'1', '!'}, [0x03] = {'2', '\''}, [0x04] = {'3', '^'}, [0x05] = {'4', '+'},
    [0x06] = {'5', '%'}, [0x07] = {'6', '&'}, [0x08] = {'7', '/'}, [0x09] = {'8', '('},
    [0x0A] = {'9', ')'}, [0x0B] = {'0', '='}, [0x0C] = {'*', '?'}, [0x0D] = {'-', '_'},
    [0x0E] = {8, 8}, [0x0F] = {'\t', '\t'},
    [0x10] = {'q', 'Q'}, [0x11] = {'w', 'W'}, [0x12] = {'e', 'E'}, [0x13] = {'r', 'R'},
    [0x14] = {'t', 'T'}, [0x15] = {'y', 'Y'}, [0x16] = {'u', 'U'}, [0x17] = {0x8A, 'I'},
    [0x18] = {'o', 'O'}, [0x19] = {'p', 'P'}, [0x1A] = {0x82, 0x83}, [0x1B] = {0x88, 0x89},
    [0x1C] = {'\n', '\n'},
    [0x1E] = {'a', 'A'}, [0x1F] = {'s', 'S'}, [0x20] = {'d', 'D'}, [0x21] = {'f', 'F'},
    [0x22] = {'g', 'G'}, [0x23] = {'h', 'H'}, [0x24] = {'j', 'J'}, [0x25] = {'k', 'K'},
    [0x26] = {'l', 'L'}, [0x27] = {0x86, 0x87}, [0x28] = {'i', 0x8B},
    [0x29] = {'"', '"'},
    [0x2B] = {',', ';'},
    [0x2C] = {'z', 'Z'}, [0x2D] = {'x', 'X'}, [0x2E] = {'c', 'C'}, [0x2F] = {'v', 'V'},
    [0x30] = {'b', 'B'}, [0x31] = {'n', 'N'}, [0x32] = {'m', 'M'},
    [0x33] = {0x84, 0x85}, [0x34] = {0x80, 0x81}, [0x35] = {'.', ':'},
    [0x39] = {' ', ' '},
};

#endif
