/* KerneliaOS .wind ayristiricisi. Bkz. wind.h */
#include "wind.h"

static unsigned wind_rd16(const unsigned char *p){ return p[0] | ((unsigned)p[1] << 8); }
static unsigned wind_rd32(const unsigned char *p){
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int wind_parse(const unsigned char *img, unsigned n, wind_prog *out){
    if(n < WIND_HDR_SIZE) return WIND_E_SHORT;
    if(img[0] != 'W' || img[1] != 'I' || img[2] != 'N' || img[3] != 'D') return WIND_E_MAGIC;
    if(wind_rd16(img + 4) != 1 || wind_rd16(img + 6) != 0) return WIND_E_VERSION;

    unsigned entry = wind_rd32(img + 8);
    unsigned code_size = wind_rd32(img + 12);
    unsigned mem_size = wind_rd32(img + 16);

    /* Kod, dosyanin geri kalaninda tamamen olmali; 32 bit tasmasina karsi kontrol */
    if(code_size == 0 || code_size > n - WIND_HDR_SIZE) return WIND_E_SIZE;
    if(entry >= code_size) return WIND_E_ENTRY;
    if(mem_size < code_size || mem_size > WIND_MAX_MEM) return WIND_E_MEM;

    out->entry = entry;
    out->code_size = code_size;
    out->mem_size = mem_size;
    out->code = img + WIND_HDR_SIZE;
    return WIND_OK;
}

const char *wind_err_text(int e){
    switch(e){
    case WIND_E_SHORT:   return "WIND: DOSYA COK KISA";
    case WIND_E_MAGIC:   return "WIND: GECERSIZ DOSYA (WIND BASLIGI YOK)";
    case WIND_E_VERSION: return "WIND: DESTEKLENMEYEN SURUM";
    case WIND_E_SIZE:    return "WIND: KOD BOYUTU GECERSIZ";
    case WIND_E_ENTRY:   return "WIND: GIRIS NOKTASI GECERSIZ";
    case WIND_E_MEM:     return "WIND: BELLEK BOYUTU GECERSIZ";
    default:             return "WIND: BILINMEYEN HATA";
    }
}
