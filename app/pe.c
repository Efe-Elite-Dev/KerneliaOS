/* KerneliaOS PE32 (i386) yukleyici. Bkz. pe.h
   Tum erisimler sinir kontrollu: bozuk bir dosya ya da tablo yukleyiciyi tasirmaz,
   sadece hata koduyla durur. */
#include "pe.h"

static unsigned pe_rd16(const unsigned char *p){ return p[0] | ((unsigned)p[1] << 8); }
static unsigned pe_rd32(const unsigned char *p){
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static void pe_wr32(unsigned char *p, unsigned v){
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* [ofs, ofs+len) aralik limit icinde mi? (tasma kontrolu dahil) */
static int pe_in(unsigned ofs, unsigned len, unsigned limit){
    return ofs <= limit && len <= limit - ofs;
}

/* ofs'ten baslayan NUL sonlu metin; limit icinde bitmezse NULL */
static const char *pe_cstr(const unsigned char *dst, unsigned limit, unsigned ofs){
    if(!pe_in(ofs, 0, limit)) return 0;
    for(unsigned i = ofs; i < limit; i++) if(dst[i] == 0) return (const char *)(dst + ofs);
    return 0;
}

int pe_parse(const unsigned char *img, unsigned n, pe_info *out){
    if(n < 0x40) return PE_E_SHORT;
    if(img[0] != 'M' || img[1] != 'Z') return PE_E_MZ;
    unsigned lfa = pe_rd32(img + 0x3C);
    if(!pe_in(lfa, 24, n)) return PE_E_SHORT;
    if(img[lfa] != 'P' || img[lfa + 1] != 'E' || img[lfa + 2] != 0 || img[lfa + 3] != 0) return PE_E_PE;

    const unsigned char *coff = img + lfa + 4;
    if(pe_rd16(coff) != 0x14C) return PE_E_MACHINE;        /* i386 */
    unsigned nsec = pe_rd16(coff + 2);
    unsigned optsz = pe_rd16(coff + 16);
    unsigned opt = lfa + 24;
    if(optsz < 96 || !pe_in(opt, optsz, n)) return PE_E_RANGE;
    if(pe_rd16(img + opt) != 0x10B) return PE_E_MACHINE;   /* PE32 (PE32+ degil) */

    unsigned entry = pe_rd32(img + opt + 16);
    unsigned base = pe_rd32(img + opt + 28);
    unsigned imgsize = pe_rd32(img + opt + 56);
    unsigned hdrsize = pe_rd32(img + opt + 60);
    if(imgsize == 0 || entry >= imgsize || hdrsize > imgsize) return PE_E_RANGE;

    unsigned secs = opt + optsz;
    if(!pe_in(secs, nsec * 40, n)) return PE_E_RANGE;
    for(unsigned i = 0; i < nsec; i++){
        const unsigned char *s = img + secs + i * 40;
        unsigned vsz = pe_rd32(s + 8), va = pe_rd32(s + 12);
        unsigned rsz = pe_rd32(s + 16), rptr = pe_rd32(s + 20);
        unsigned vmax = vsz > rsz ? vsz : rsz;
        if(!pe_in(va, vmax, imgsize)) return PE_E_RANGE;
        if(rsz && !pe_in(rptr, rsz, n)) return PE_E_RANGE;
    }

    out->entry_rva = entry;
    out->image_base = base;
    out->image_size = imgsize;
    out->nsections = nsec;
    out->nimports = 0;
    out->nrelocs = 0;
    return PE_OK;
}

int pe_load(const unsigned char *img, unsigned n,
            unsigned char *dst, unsigned dst_size, unsigned load_addr,
            pe_resolve_fn res, void *ctx, pe_info *out, unsigned *entry_out){
    pe_info info;
    int r = pe_parse(img, n, &info);
    if(r != PE_OK) return r;
    if(info.image_size > dst_size) return PE_E_SIZE;

    unsigned lfa = pe_rd32(img + 0x3C);
    unsigned opt = lfa + 24;
    unsigned optsz = pe_rd16(img + lfa + 4 + 16);
    unsigned secs = opt + optsz;
    unsigned ndirs = pe_rd32(img + opt + 92);
    if(ndirs > 16) ndirs = 16;
    const unsigned char *dirs = img + opt + 96;        /* 8 bayt: VA, boyut */

    /* 1) bellek: sifirla, basliklari ve bolumleri yerlestir */
    for(unsigned i = 0; i < info.image_size; i++) dst[i] = 0;
    unsigned hdr = pe_rd32(img + opt + 60);
    if(hdr > n) hdr = n;
    for(unsigned i = 0; i < hdr; i++) dst[i] = img[i];
    for(unsigned i = 0; i < info.nsections; i++){
        const unsigned char *s = img + secs + i * 40;
        unsigned rsz = pe_rd32(s + 16), rptr = pe_rd32(s + 20), va = pe_rd32(s + 12);
        for(unsigned k = 0; k < rsz; k++) dst[va + k] = img[rptr + k];
    }

    /* 2) base relocation (yukleme adresi farkliysa) */
    unsigned delta = load_addr - info.image_base;
    if(delta != 0 && ndirs > 5){
        unsigned rva = pe_rd32(dirs + 5 * 8), sz = pe_rd32(dirs + 5 * 8 + 4);
        if(sz){
            if(!pe_in(rva, sz, info.image_size)) return PE_E_RANGE;
            unsigned p = rva, end = rva + sz;
            while(p + 8 <= end){
                unsigned page = pe_rd32(dst + p);
                unsigned bsz = pe_rd32(dst + p + 4);
                if(bsz < 8 || bsz > end - p) return PE_E_RANGE;
                for(unsigned e = p + 8; e + 2 <= p + bsz; e += 2){
                    unsigned ent = pe_rd16(dst + e);
                    unsigned type = ent >> 12;
                    if(type == 0) continue;                     /* ABSOLUTE: dolgu */
                    if(type != 3) return PE_E_RELOC;            /* HIGHLOW: i386 */
                    unsigned target = page + (ent & 0xFFF);
                    if(!pe_in(target, 4, info.image_size)) return PE_E_RANGE;
                    pe_wr32(dst + target, pe_rd32(dst + target) + delta);
                    info.nrelocs++;
                }
                p += bsz;
            }
        }
    }

    /* 3) import tablosu: her fonksiyonun IAT girisine cozulen adres yazilir */
    unsigned irva = pe_rd32(dirs + 1 * 8), isz = pe_rd32(dirs + 1 * 8 + 4);
    if(irva && isz){
        unsigned d = irva;
        for(;;){
            if(!pe_in(d, 20, info.image_size)) return PE_E_RANGE;
            unsigned oft = pe_rd32(dst + d), name = pe_rd32(dst + d + 12), ft = pe_rd32(dst + d + 16);
            if(oft == 0 && name == 0 && ft == 0) break;
            const char *dll = pe_cstr(dst, info.image_size, name);
            if(!dll) return PE_E_RANGE;
            unsigned ilt = oft ? oft : ft;
            for(unsigned i = 0;; i++){
                if(!pe_in(ilt + 4 * i, 4, info.image_size)) return PE_E_RANGE;
                unsigned th = pe_rd32(dst + ilt + 4 * i);
                if(th == 0) break;
                if(!pe_in(ft + 4 * i, 4, info.image_size)) return PE_E_RANGE;
                unsigned addr;
                if(th & 0x80000000u){
                    addr = res(ctx, dll, 0, th & 0xFFFF);
                } else {
                    if(!pe_in(th, 3, info.image_size)) return PE_E_RANGE;
                    const char *fname = pe_cstr(dst, info.image_size, th + 2);
                    if(!fname) return PE_E_RANGE;
                    addr = res(ctx, dll, fname, 0);
                }
                if(!addr) return PE_E_IMPORT;
                pe_wr32(dst + ft + 4 * i, addr);
                info.nimports++;
            }
            d += 20;
        }
    }

    if(out) *out = info;
    if(entry_out) *entry_out = load_addr + info.entry_rva;
    return PE_OK;
}

const char *pe_err_text(int e){
    switch(e){
    case PE_E_SHORT:   return "PE: DOSYA COK KISA";
    case PE_E_MZ:      return "PE: GECERSIZ DOSYA (MZ YOK)";
    case PE_E_PE:      return "PE: GECERSIZ DOSYA (PE BASLIGI YOK)";
    case PE_E_MACHINE: return "PE: DESTEKLENMEYEN MIMARI (PE32 I386 GEREKLI)";
    case PE_E_RANGE:   return "PE: TABLO SINIRLARI DISINDA";
    case PE_E_SIZE:    return "PE: PROGRAM BELLEGE SIGMIYOR";
    case PE_E_RELOC:   return "PE: DESTEKLENMEYEN RELOCATION";
    case PE_E_IMPORT:  return "PE: IMPORT COZULEMEDI";
    default:           return "PE: BILINMEYEN HATA";
    }
}
