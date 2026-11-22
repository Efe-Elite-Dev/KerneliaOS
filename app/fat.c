/* WindOS FAT12/FAT16 okuyucu. Bkz. fat.h */
#include "fat.h"

#define FAT_EOC 0xFFFFFFFFu

static unsigned rd16(const unsigned char *p){ return p[0] | ((unsigned)p[1] << 8); }
static unsigned rd32(const unsigned char *p){
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned clus_sec(const fat_vol *v, unsigned c){ return v->data_start + (c - 2) * v->spc; }

/* FAT'taki bir bayt (1 sektorluk onbellek ile) */
static int fat_byte(fat_vol *v, unsigned off, unsigned char *out){
    if(off >= v->fat_size * v->bps) return -1;
    unsigned sec = v->fat_start + off / v->bps;
    if(sec != v->cache_sec){
        if(v->rd(v->ctx, sec, 1, v->cache) != 0) return -1;
        v->cache_sec = sec;
    }
    *out = v->cache[off % v->bps];
    return 0;
}

/* c kumesinin zincirdeki sonraki kumesi; sonu/hata -> FAT_EOC */
static unsigned fat_next(fat_vol *v, unsigned c){
    unsigned char a, b;
    unsigned val;
    if(v->type == 12){
        unsigned off = c + c / 2;
        if(fat_byte(v, off, &a) || fat_byte(v, off + 1, &b)) return FAT_EOC;
        val = (c & 1) ? ((unsigned)(a >> 4) | ((unsigned)b << 4))
                      : ((unsigned)a | (((unsigned)b & 0x0F) << 8));
        if(val >= 0xFF8 || val == 0xFF7) return FAT_EOC;
    } else {
        unsigned off = c * 2;
        if(fat_byte(v, off, &a) || fat_byte(v, off + 1, &b)) return FAT_EOC;
        val = (unsigned)a | ((unsigned)b << 8);
        if(val >= 0xFFF8 || val == 0xFFF7) return FAT_EOC;
    }
    if(val < 2) return FAT_EOC;
    return val;
}

int fat_mount(fat_vol *v, fat_read_fn rd, void *ctx){
    unsigned char b[512];
    v->rd = rd;
    v->ctx = ctx;
    v->cache_sec = 0xFFFFFFFFu;
    if(rd(ctx, 0, 1, b) != 0) return -1;
    if(b[510] != 0x55 || b[511] != 0xAA) return -1;
    v->bps = rd16(b + 11);
    v->spc = b[13];
    unsigned reserved = rd16(b + 14);
    unsigned nfats = b[16];
    unsigned root_ent = rd16(b + 17);
    unsigned tot16 = rd16(b + 19);
    unsigned fat16 = rd16(b + 22);
    unsigned tot32 = rd32(b + 32);
    if(v->bps != 512 || v->spc == 0 || nfats == 0 || fat16 == 0) return -1;
    unsigned total = tot16 ? tot16 : tot32;
    v->fat_start = reserved;
    v->fat_size = fat16;
    v->root_sectors = (root_ent * 32 + 511) / 512;
    v->root_start = reserved + nfats * fat16;
    v->data_start = v->root_start + v->root_sectors;
    if(total <= v->data_start) return -1;
    v->clusters = (total - v->data_start) / v->spc;
    v->type = (v->clusters < 4085) ? 12 : 16;
    return 0;
}

/* "name.ext" -> 11 baytlik 8.3 adi (buyuk harf, bosluk dolgulu). Gecersizse -1 */
static int to83(const char *s, int len, unsigned char out[11]){
    int dot = -1;
    for(int i = 0; i < 11; i++) out[i] = ' ';
    for(int i = 0; i < len; i++) if(s[i] == '.') dot = i;
    int blen = (dot >= 0) ? dot : len;
    int elen = (dot >= 0) ? len - dot - 1 : 0;
    if(blen < 1 || blen > 8 || elen > 3) return -1;
    for(int i = 0; i < blen; i++){
        char c = s[i];
        if(c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[i] = (unsigned char)c;
    }
    for(int i = 0; i < elen; i++){
        char c = s[dot + 1 + i];
        if(c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[8 + i] = (unsigned char)c;
    }
    return 0;
}

static int name_eq(const unsigned char *a, const unsigned char *b){
    for(int i = 0; i < 11; i++) if(a[i] != b[i]) return 0;
    return 1;
}

/* ---- Uzun dosya adi (LFN) ----
   Bir ad, once 0x0F oznitelikli LFN girisleri (sondan basa, 13 karakter/giris)
   sonra 8.3 kisa girisi olarak saklanir. Kisa girisin sagla topla (checksum) LFN
   zincirindeki degerle eslesirse zincir o kisa girise aittir. */
#define FAT_LFN_MAX 80          /* en fazla bu kadar karakterli uzun ad (ASCII) */
#define FAT_ATTR_LFN 0x0F

static unsigned char lfn_checksum(const unsigned char *s){
    unsigned char sum = 0;
    for(int i = 0; i < 11; i++) sum = (unsigned char)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + s[i]);
    return sum;
}

static int ch_eq(unsigned char a, unsigned char b){
    if(a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
    if(b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
    return a == b;
}

/* LFN girisinin 13 UCS-2 karakterini nm tamponuna yerlestirir (ASCII disi -> '?') */
static void lfn_put(unsigned char *nm, const unsigned char *d, unsigned ord){
    static const unsigned char off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for(unsigned j = 0; j < 13; j++){
        unsigned idx = (ord - 1) * 13 + j;
        unsigned u;
        if(idx >= FAT_LFN_MAX) return;
        u = rd16(d + off[j]);
        nm[idx] = (u == 0 || u == 0xFFFF) ? 0 : (u < 0x80 ? (unsigned char)u : (unsigned char)'?');
    }
}

/* nm (0 ile sonlanan) istenen ad mi? Buyuk/kucuk harf farketmez */
static int lfn_name_eq(const unsigned char *nm, const char *name, int len){
    int n = 0;
    while(n < FAT_LFN_MAX && nm[n]) n++;
    if(n == FAT_LFN_MAX || n != len) return 0;
    for(int i = 0; i < len; i++) if(!ch_eq(nm[i], (unsigned char)name[i])) return 0;
    return 1;
}

/* first kumesinden baslayan dizinde (first==0: kok dizin) adi arar.
   name: kullanicinin yazdigi bilesen (8.3 ya da uzun ad) */
static int dir_find(fat_vol *v, unsigned first, const char *name, int len, fat_file *out){
    unsigned char sec[512];
    unsigned char s83[11];
    int have83 = (to83(name, len, s83) == 0);
    unsigned char lfn[FAT_LFN_MAX];
    int lfn_have = 0, lfn_ok = 0;   /* zincir goruldu mu / gecerli mi */
    unsigned char lfn_sum = 0;
    unsigned cl = first, si = 0;
    for(unsigned i = 0; i < FAT_LFN_MAX; i++) lfn[i] = 0;
    for(;;){
        unsigned lba;
        if(first == 0){
            if(si >= v->root_sectors) return -1;
            lba = v->root_start + si;
        } else {
            if(cl < 2 || cl - 2 >= v->clusters) return -1;
            lba = clus_sec(v, cl) + si;
        }
        if(v->rd(v->ctx, lba, 1, sec) != 0) return -1;
        for(unsigned e = 0; e < 512 / 32; e++){
            const unsigned char *d = sec + e * 32;
            unsigned ord = d[0] & 0x1F;
            int hit = 0;
            if(d[0] == 0x00) return -1;                 /* dizin sonu */
            if(d[0] == 0xE5){ lfn_have = 0; continue; }  /* silinmis giris */
            if(d[11] == FAT_ATTR_LFN){                   /* uzun ad girisi */
                if(d[0] & 0x40){                         /* zincirin son (en ust) parcasi */
                    for(unsigned i = 0; i < FAT_LFN_MAX; i++) lfn[i] = 0;
                    lfn_sum = d[13];
                    lfn_have = 1;
                    lfn_ok = (ord >= 1 && ord <= 20);
                } else if(!lfn_have || d[13] != lfn_sum || ord < 1 || ord > 20){
                    lfn_ok = 0;
                }
                if(lfn_ok) lfn_put(lfn, d, ord);
                continue;
            }
            /* kisa (8.3) giris: once uzun ad zincirine, sonra 8.3 adina bak */
            if(lfn_have && lfn_ok && lfn_sum == lfn_checksum(d)) hit = lfn_name_eq(lfn, name, len);
            if(!hit && have83 && !(d[11] & 0x08)) hit = name_eq(d, s83);
            lfn_have = 0;
            if(d[11] & 0x08) continue;                   /* birim etiketi */
            if(hit){
                out->first = rd16(d + 26);
                out->size = rd32(d + 28);
                out->is_dir = (d[11] & 0x10) ? 1 : 0;
                out->pos = 0;
                out->cur = out->first;
                return 0;
            }
        }
        if(first != 0 && ++si == v->spc){
            si = 0;
            cl = fat_next(v, cl);
            if(cl == FAT_EOC) return -1;
        } else if(first == 0){
            si++;
        }
    }
}

int fat_open(fat_vol *v, const char *path, fat_file *f){
    const char *p = path;
    unsigned first = 0;
    fat_file cur;
    while(*p == '/') p++;
    if(!*p){                       /* kok dizin */
        f->first = 0; f->size = 0; f->pos = 0; f->cur = 0; f->is_dir = 1;
        return 0;
    }
    for(;;){
        const char *e = p;
        while(*e && *e != '/') e++;
        int len = (int)(e - p);
        if(len == 0 || len > FAT_LFN_MAX) return -1;
        if(dir_find(v, first, p, len, &cur) != 0) return -1;
        if(*e == 0){ *f = cur; return 0; }
        if(!cur.is_dir) return -1;
        first = cur.first;
        p = e;
        while(*p == '/') p++;
        if(!*p){ *f = cur; return 0; }   /* sondaki '/' : dizinin kendisi */
    }
}

int fat_read(fat_vol *v, fat_file *f, unsigned char *buf, unsigned n){
    unsigned char sec[512];
    unsigned csz = v->spc * v->bps;
    unsigned done = 0;
    if(f->is_dir) return -1;
    while(done < n && f->pos < f->size){
        unsigned off = f->pos % csz;
        if(off == 0 && f->pos > 0){
            f->cur = fat_next(v, f->cur);
            if(f->cur == FAT_EOC) break;
        }
        if(f->cur < 2 || f->cur - 2 >= v->clusters) return -1;
        unsigned sidx = off / 512, sb = off % 512;
        if(v->rd(v->ctx, clus_sec(v, f->cur) + sidx, 1, sec) != 0) return -1;
        unsigned take = 512 - sb;
        if(take > n - done) take = n - done;
        if(take > f->size - f->pos) take = f->size - f->pos;
        for(unsigned k = 0; k < take; k++) buf[done + k] = sec[sb + k];
        done += take;
        f->pos += take;
    }
    return (int)done;
}
