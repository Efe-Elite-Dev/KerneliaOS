/* WindOS FAT12/FAT16 surucu: okuma, LFN'li dosya/klasor olusturma, listeleme. Bkz. fat.h */
#include "fat.h"

#define FAT_EOC 0xFFFFFFFFu
#define FAT_NAME_BUF (FAT_NAME_MAX + 1)  /* uzun ad tamponu (0 ile sonlanir) */
#define FAT_PATH_MAX 512
#define FAT_ATTR_LFN 0x0F
#define FAT_DIR_PER 16                   /* bir sektorde 32 baytlik dizin girisi */

static unsigned rd16(const unsigned char *p){ return p[0] | ((unsigned)p[1] << 8); }
static unsigned rd32(const unsigned char *p){
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static void fat_w16(unsigned char *p, unsigned v){ p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void fat_w32(unsigned char *p, unsigned v){
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static unsigned clus_sec(const fat_vol *v, unsigned c){ return v->data_start + (c - 2) * v->spc; }
static unsigned fat_eoc(const fat_vol *v){ return v->type == 12 ? 0xFFF : 0xFFFF; }

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

/* FAT'taki bir baytı tum kopyalarda yazar. Onbellegi gecersiz kilar */
static int fat_put_byte(fat_vol *v, unsigned off, unsigned char val){
    unsigned char sec[512];
    if(!v->wr || off >= v->fat_size * v->bps) return -1;
    v->cache_sec = 0xFFFFFFFFu;
    for(unsigned k = 0; k < v->nfats; k++){
        unsigned lba = v->fat_start + k * v->fat_size + off / v->bps;
        if(v->rd(v->ctx, lba, 1, sec) != 0) return -1;
        sec[off % v->bps] = val;
        if(v->wr(v->ctx, lba, 1, sec) != 0) return -1;
    }
    return 0;
}

/* c kumesinin FAT'taki ham degeri (0 = bos, FAT_EOC = okuma hatasi) */
static unsigned fat_raw_get(fat_vol *v, unsigned c){
    unsigned char a, b;
    if(v->type == 12){
        unsigned off = c + c / 2;
        if(fat_byte(v, off, &a) || fat_byte(v, off + 1, &b)) return FAT_EOC;
        return (c & 1) ? ((unsigned)(a >> 4) | ((unsigned)b << 4))
                       : ((unsigned)a | (((unsigned)b & 0x0F) << 8));
    }
    if(fat_byte(v, c * 2, &a) || fat_byte(v, c * 2 + 1, &b)) return FAT_EOC;
    return (unsigned)a | ((unsigned)b << 8);
}

/* c kumesinin FAT'taki ham degerini yazar (tum kopyalarda) */
static int fat_raw_put(fat_vol *v, unsigned c, unsigned val){
    unsigned char a, b;
    if(v->type == 12){
        unsigned off = c + c / 2;
        if(fat_byte(v, off, &a) || fat_byte(v, off + 1, &b)) return -1;
        if(c & 1){
            a = (unsigned char)((a & 0x0F) | ((val & 0x0F) << 4));
            b = (unsigned char)((val >> 4) & 0xFF);
        } else {
            a = (unsigned char)(val & 0xFF);
            b = (unsigned char)((b & 0xF0) | ((val >> 8) & 0x0F));
        }
        return (fat_put_byte(v, off, a) || fat_put_byte(v, off + 1, b)) ? -1 : 0;
    }
    return (fat_put_byte(v, c * 2, (unsigned char)(val & 0xFF)) ||
            fat_put_byte(v, c * 2 + 1, (unsigned char)((val >> 8) & 0xFF))) ? -1 : 0;
}

/* c kumesinin zincirdeki sonraki kumesi; sonu/hata -> FAT_EOC */
static unsigned fat_next(fat_vol *v, unsigned c){
    unsigned val = fat_raw_get(v, c);
    if(val == FAT_EOC) return FAT_EOC;
    if(v->type == 12){
        if(val >= 0xFF8 || val == 0xFF7) return FAT_EOC;
    } else {
        if(val >= 0xFFF8 || val == 0xFFF7) return FAT_EOC;
    }
    if(val < 2) return FAT_EOC;
    return val;
}

/* Bos (ham degeri 0) ilk kumeyi from'dan itibaren arar. Yoksa 0 */
static unsigned fat_find_free(fat_vol *v, unsigned from){
    for(unsigned c = from; c < v->clusters + 2; c++) if(fat_raw_get(v, c) == 0) return c;
    return 0;
}

static unsigned fat_free_count(fat_vol *v){
    unsigned n = 0;
    for(unsigned c = 2; c < v->clusters + 2; c++) if(fat_raw_get(v, c) == 0) n++;
    return n;
}

int fat_mount(fat_vol *v, fat_read_fn rd, void *ctx){
    unsigned char b[512];
    v->rd = rd;
    v->wr = 0;
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
    v->nfats = nfats;
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

void fat_set_writer(fat_vol *v, fat_write_fn wr){ v->wr = wr; }

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
        if(idx >= FAT_NAME_BUF) return;
        u = rd16(d + off[j]);
        nm[idx] = (u == 0 || u == 0xFFFF) ? 0 : (u < 0x80 ? (unsigned char)u : (unsigned char)'?');
    }
}

/* nm (0 ile sonlanan) istenen ad mi? Buyuk/kucuk harf farketmez */
static int lfn_name_eq(const unsigned char *nm, const char *name, int len){
    int n = 0;
    while(n < FAT_NAME_BUF && nm[n]) n++;
    if(n == FAT_NAME_BUF || n != len) return 0;
    for(int i = 0; i < len; i++) if(!ch_eq(nm[i], (unsigned char)name[i])) return 0;
    return 1;
}

/* Bir LFN girisini zincire ekler. have/ok/sum: zincir durumu (dizin taramasinda korunur) */
static void lfn_step(unsigned char *lfn, int *have, int *ok, unsigned char *sum, const unsigned char *d){
    unsigned ord = d[0] & 0x1F;
    if(d[0] & 0x40){                         /* zincirin son (en ust) parcasi */
        for(unsigned i = 0; i < FAT_NAME_BUF; i++) lfn[i] = 0;
        *sum = d[13];
        *have = 1;
        *ok = (ord >= 1 && ord <= 20);
    } else if(!*have || d[13] != *sum || ord < 1 || ord > 20){
        *ok = 0;
    }
    if(*ok) lfn_put(lfn, d, ord);
}

/* first kumesinden baslayan dizinde (first==0: kok dizin) adi arar.
   name: kullanicinin yazdigi bilesen (8.3 ya da uzun ad) */
static int dir_find(fat_vol *v, unsigned first, const char *name, int len, fat_file *out){
    unsigned char sec[512];
    unsigned char s83[11];
    int have83 = (to83(name, len, s83) == 0);
    unsigned char lfn[FAT_NAME_BUF];
    int lfn_have = 0, lfn_ok = 0;   /* zincir goruldu mu / gecerli mi */
    unsigned char lfn_sum = 0;
    unsigned cl = first, si = 0;
    for(unsigned i = 0; i < FAT_NAME_BUF; i++) lfn[i] = 0;
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
            int hit = 0;
            if(d[0] == 0x00) return -1;                  /* dizin sonu */
            if(d[0] == 0xE5){ lfn_have = 0; continue; }  /* silinmis giris */
            if(d[11] == FAT_ATTR_LFN){ lfn_step(lfn, &lfn_have, &lfn_ok, &lfn_sum, d); continue; }
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
        if(len == 0 || len > FAT_NAME_MAX) return -1;
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

/* ---- Dizin girisi yardimcilari ---- */

/* Dizin girisi idx -> sektor ve bayt konumu. Donus 0 tamam, 1 zincir sonu, -1 hata */
static int dir_pos(fat_vol *v, unsigned first, unsigned idx, unsigned *lba, unsigned *off){
    if(first == 0){
        if(idx >= v->root_sectors * FAT_DIR_PER) return 1;
        *lba = v->root_start + idx / FAT_DIR_PER;
        *off = (idx % FAT_DIR_PER) * 32;
        return 0;
    }
    unsigned cper = v->spc * FAT_DIR_PER;
    unsigned cl = first, k = idx / cper;
    for(; k > 0; k--){
        cl = fat_next(v, cl);
        if(cl == FAT_EOC) return 1;
    }
    if(cl < 2 || cl - 2 >= v->clusters) return -1;
    unsigned r = idx % cper;
    *lba = clus_sec(v, cl) + r / FAT_DIR_PER;
    *off = (r % FAT_DIR_PER) * 32;
    return 0;
}

static int dir_read(fat_vol *v, unsigned first, unsigned idx, unsigned char *e){
    unsigned char sec[512];
    unsigned lba, off;
    int r = dir_pos(v, first, idx, &lba, &off);
    if(r != 0) return r;
    if(v->rd(v->ctx, lba, 1, sec) != 0) return -1;
    for(unsigned i = 0; i < 32; i++) e[i] = sec[off + i];
    return 0;
}

static int dir_write(fat_vol *v, unsigned first, unsigned idx, const unsigned char *e){
    unsigned char sec[512];
    unsigned lba, off;
    if(!v->wr || dir_pos(v, first, idx, &lba, &off) != 0) return -1;
    if(v->rd(v->ctx, lba, 1, sec) != 0) return -1;
    for(unsigned i = 0; i < 32; i++) sec[off + i] = e[i];
    return v->wr(v->ctx, lba, 1, sec);
}

/* Dizinin mevcut giris sayisi (zincirdeki kumeler dahil) */
static unsigned dir_total(fat_vol *v, unsigned first){
    if(first == 0) return v->root_sectors * FAT_DIR_PER;
    unsigned n = 1, cl = first;
    while((cl = fat_next(v, cl)) != FAT_EOC) n++;
    return n * v->spc * FAT_DIR_PER;
}

/* Alt dizinin zincirine bir kume ekler; yeni kume sifirlanir (tum girisler bos) */
static int dir_extend(fat_vol *v, unsigned first){
    unsigned char zero[512];
    unsigned last = first, c;
    for(unsigned i = 0; i < 512; i++) zero[i] = 0;
    for(;;){
        unsigned n = fat_next(v, last);
        if(n == FAT_EOC) break;
        last = n;
    }
    c = fat_find_free(v, 2);
    if(!c) return -1;
    if(fat_raw_put(v, last, c) != 0 || fat_raw_put(v, c, fat_eoc(v)) != 0) return -1;
    for(unsigned s = 0; s < v->spc; s++)
        if(v->wr(v->ctx, clus_sec(v, c) + s, 1, zero) != 0) return -1;
    return 0;
}

/* need ardisik bos giris arar. Mevcut girisler icinde bulursa *start'a yazar ve 0 doner.
   Bulamazsa (zincir sonu) *start = sondaki bos araligin basi ve 1 doner. */
static int dir_find_run(fat_vol *v, unsigned first, unsigned need, unsigned *start){
    unsigned char e[32];
    unsigned idx = 0, run = 0, run_start = 0;
    for(;;){
        int r = dir_read(v, first, idx, e);
        if(r == 1) break;
        if(r < 0) return -1;
        if(e[0] == 0x00 || e[0] == 0xE5){
            if(run == 0) run_start = idx;
            run++;
            if(run >= need){ *start = run_start; return 0; }
        } else {
            run = 0;
        }
        idx++;
    }
    *start = (run > 0) ? run_start : idx;
    return 1;
}

/* Dizinde 11 baytlik kisa ad var mi? 1 var, 0 yok, -1 hata */
static int dir_has_short(fat_vol *v, unsigned first, const unsigned char *s11){
    unsigned char e[32];
    for(unsigned idx = 0;; idx++){
        int r = dir_read(v, first, idx, e);
        if(r == 1) return 0;
        if(r < 0) return -1;
        if(e[0] == 0x00) return 0;
        if(e[0] == 0xE5 || e[11] == FAT_ATTR_LFN || (e[11] & 0x08)) continue;
        if(name_eq(e, s11)) return 1;
    }
}

/* "name.ext" -> 8.3 adi (kucuk harf ve kisa ad icin). Donus 1: dogrudan 8.3 kullanilabilir */
static int short_ok(const char *s, int len, unsigned char out[11]){
    int dots = 0;
    if(to83(s, len, out) != 0) return 0;
    for(int i = 0; i < len; i++){
        char c = s[i];
        if(c == '.'){ dots++; continue; }
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '~')) return 0;
    }
    return dots <= 1;
}

static int name_bad(const char *s, int len){
    for(int i = 0; i < len; i++){
        unsigned char c = (unsigned char)s[i];
        if(c < 0x20 || c > 0x7E) return 1;
        if(c == '"' || c == '*' || c == '/' || c == ':' || c == '<' || c == '>' ||
           c == '?' || c == '\\' || c == '|') return 1;
    }
    return 0;
}

static int is_alnum_ascii(char c){
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
static unsigned char up_ascii(char c){
    return (unsigned char)((c >= 'a' && c <= 'z') ? c - 32 : c);
}

/* Uzun ad icin benzersiz 8.3 takma ad: ILK~N.EXT. N = 1..9999; N'nin basamagi kadar taban
   kisalir (N 1..9 icin 6 karakter, 10..99 icin 5 ...), boylece ad 8 karakteri asmaz. */
static int make_alias(fat_vol *v, unsigned first, const char *name, int len, unsigned char out[11]){
    unsigned char base[8], ext[3];
    int dot = -1, nb = 0, ne = 0;
    for(int i = 0; i < len; i++) if(name[i] == '.') dot = i;
    int blen = (dot >= 0) ? dot : len;
    for(int i = 0; i < blen && nb < 8; i++) if(is_alnum_ascii(name[i])) base[nb++] = up_ascii(name[i]);
    if(dot >= 0)
        for(int i = dot + 1; i < len && ne < 3; i++) if(is_alnum_ascii(name[i])) ext[ne++] = up_ascii(name[i]);
    if(nb == 0){ base[0] = '_'; nb = 1; }
    for(unsigned n = 1; n <= 9999; n++){
        unsigned char s[11];
        unsigned nd = (n < 10) ? 1 : (n < 100) ? 2 : (n < 1000) ? 3 : 4;
        unsigned bl = ((unsigned)nb < 7 - nd) ? (unsigned)nb : 7 - nd;   /* taban + '~' + N <= 8 */
        unsigned q = n;
        for(int k = 0; k < 11; k++) s[k] = ' ';
        for(unsigned i = 0; i < bl; i++) s[i] = base[i];
        s[bl] = '~';
        for(unsigned k = 0; k < nd; k++){                                  /* N'nin basamaklari */
            s[bl + nd - k] = (unsigned char)('0' + q % 10);
            q /= 10;
        }
        for(int i = 0; i < ne; i++) s[8 + i] = ext[i];
        int r = dir_has_short(v, first, s);
        if(r < 0) return -1;
        if(r == 0){
            for(int k = 0; k < 11; k++) out[k] = s[k];
            return 0;
        }
    }
    return -1;
}

/* Uzun ad girisi: seq (1..n) parcasi; last: zincirin en ustu (0x40 bayragi) */
static void lfn_build(unsigned char *e, const char *name, int nlen, unsigned seq, int last, unsigned char sum){
    static const unsigned char off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for(unsigned i = 0; i < 32; i++) e[i] = 0;
    e[0] = (unsigned char)(seq | (last ? 0x40 : 0));
    e[11] = FAT_ATTR_LFN;
    e[13] = sum;
    for(unsigned j = 0; j < 13; j++){
        unsigned idx = (seq - 1) * 13 + j;
        unsigned u;
        if(idx < (unsigned)nlen) u = (unsigned char)name[idx];
        else if(idx == (unsigned)nlen) u = 0;          /* sonlandirici */
        else u = 0xFFFF;                               /* dolgu */
        fat_w16(e + off[j], u);
    }
}

/* 8.3 girisini "AD.UZ" metnine cevirir (bosluklar atilir). out en az 13 bayt */
static void fmt83(const unsigned char *e, char *out){
    int k = 0;
    for(int i = 0; i < 8 && e[i] != ' '; i++) out[k++] = (char)e[i];
    if(e[8] != ' '){
        out[k++] = '.';
        for(int i = 8; i < 11 && e[i] != ' '; i++) out[k++] = (char)e[i];
    }
    out[k] = 0;
}

/* "." (dotdot=0) ya da ".." (dotdot=1) girisi */
static void dir_dot(unsigned char *e, int dotdot, unsigned first){
    for(unsigned i = 0; i < 32; i++) e[i] = 0;
    for(unsigned i = 0; i < 11; i++) e[i] = ' ';
    e[0] = '.';
    if(dotdot) e[1] = '.';
    e[11] = 0x10;
    fat_w16(e + 26, first);
}

/* size bayti yeni kume zincirine yazar; ilk kume *first'e konur */
static int fat_write_data(fat_vol *v, const unsigned char *data, unsigned size, unsigned *first){
    unsigned char sec[512];
    unsigned done = 0, prev = 0, cursor = 2;
    *first = 0;
    while(done < size){
        unsigned c = fat_find_free(v, cursor);
        if(!c) return -1;
        cursor = c + 1;
        if(fat_raw_put(v, c, fat_eoc(v)) != 0) return -1;
        if(prev){ if(fat_raw_put(v, prev, c) != 0) return -1; }
        else *first = c;
        for(unsigned s = 0; s < v->spc && done < size; s++){
            unsigned take = (size - done < 512) ? size - done : 512;
            for(unsigned k = 0; k < 512; k++) sec[k] = (k < take) ? data[done + k] : 0;
            if(v->wr(v->ctx, clus_sec(v, c) + s, 1, sec) != 0) return -1;
            done += take;
        }
        prev = c;
    }
    return 0;
}

/* Yeni giris icin yer plani: kisa ad, LFN giris sayisi, bos aralik, dizin genisleme */
typedef struct {
    unsigned char alias[11];
    int need_lfn;
    unsigned nlfn;
    unsigned start;
    unsigned ext;
} ent_plan;

/* parent dizininde name icin plan cikarir; diske hicbir sey yazmaz */
static int ent_plan_make(fat_vol *v, unsigned parent, const char *name, int nlen, ent_plan *p){
    unsigned need, total, cper;
    p->need_lfn = !short_ok(name, nlen, p->alias);
    if(p->need_lfn && make_alias(v, parent, name, nlen, p->alias) != 0) return -1;
    p->nlfn = p->need_lfn ? (unsigned)(nlen + 1 + 12) / 13 : 0;
    need = p->nlfn + 1;
    if(dir_find_run(v, parent, need, &p->start) < 0) return -1;
    total = dir_total(v, parent);
    p->ext = 0;
    if(p->start + need > total){
        if(parent == 0) return -1;                  /* kok dizin sabit boyutlu */
        cper = v->spc * FAT_DIR_PER;
        p->ext = (p->start + need - total + cper - 1) / cper;
    }
    return 0;
}

/* Plani uygular: gerekirse dizini genisletir, LFN ve kisa girisi yazar */
static int ent_plan_commit(fat_vol *v, unsigned parent, const char *name, int nlen,
                           const ent_plan *p, unsigned char attr, unsigned fc, unsigned size){
    unsigned char e[32];
    for(unsigned i = 0; i < p->ext; i++) if(dir_extend(v, parent) != 0) return -1;
    for(unsigned i = 0; i < p->nlfn; i++){
        unsigned seq = p->nlfn - i;
        lfn_build(e, name, nlen, seq, seq == p->nlfn, lfn_checksum(p->alias));
        if(dir_write(v, parent, p->start + i, e) != 0) return -1;
    }
    for(unsigned i = 0; i < 32; i++) e[i] = 0;
    for(unsigned i = 0; i < 11; i++) e[i] = p->alias[i];
    e[11] = attr;
    fat_w16(e + 26, fc);
    fat_w32(e + 28, size);
    return dir_write(v, parent, p->start + p->nlfn, e);
}

/* path'i ust dizin ilk kumesi ve son bilesen adina ayirir */
static int path_split(fat_vol *v, const char *path, unsigned *parent, const char **name, int *nlen){
    char pbuf[FAT_PATH_MAX];
    const char *last = 0, *q;
    unsigned plen;
    fat_file pf;
    for(q = path; *q; q++) if(*q == '/') last = q;
    *name = last ? last + 1 : path;
    plen = last ? (unsigned)(last - path) : 0;
    *nlen = 0;
    while((*name)[*nlen]) (*nlen)++;
    if(*nlen < 1 || *nlen > FAT_NAME_MAX || plen >= FAT_PATH_MAX) return -1;
    if(name_bad(*name, *nlen)) return -1;
    for(unsigned i = 0; i < plen; i++) pbuf[i] = path[i];
    pbuf[plen] = 0;
    if(fat_open(v, pbuf, &pf) != 0 || !pf.is_dir) return -1;
    *parent = pf.first;
    return 0;
}

int fat_create(fat_vol *v, const char *path, const unsigned char *data, unsigned size){
    unsigned parent, fc = 0, csz, ncl;
    const char *name;
    int nlen;
    fat_file tmp;
    ent_plan p;
    if(!v->wr) return -1;
    if(path_split(v, path, &parent, &name, &nlen) != 0) return -1;
    if(dir_find(v, parent, name, nlen, &tmp) == 0) return -1;   /* zaten var */
    if(ent_plan_make(v, parent, name, nlen, &p) != 0) return -1;
    csz = v->spc * v->bps;
    ncl = size ? (size + csz - 1) / csz : 0;
    if(fat_free_count(v) < ncl + p.ext) return -1;             /* yer yoksa hic bir sey degismez */
    if(ncl && fat_write_data(v, data, size, &fc) != 0) return -1;
    return ent_plan_commit(v, parent, name, nlen, &p, 0x20, fc, size);
}

int fat_mkdir(fat_vol *v, const char *path){
    unsigned parent, c, csz;
    const char *name;
    int nlen;
    fat_file tmp;
    ent_plan p;
    unsigned char sec[512], zero[512];
    if(!v->wr) return -1;
    if(path_split(v, path, &parent, &name, &nlen) != 0) return -1;
    if(dir_find(v, parent, name, nlen, &tmp) == 0) return -1;
    if(ent_plan_make(v, parent, name, nlen, &p) != 0) return -1;
    if(fat_free_count(v) < 1 + p.ext) return -1;
    c = fat_find_free(v, 2);
    if(!c) return -1;
    if(fat_raw_put(v, c, fat_eoc(v)) != 0) return -1;
    csz = v->spc;
    for(unsigned i = 0; i < 512; i++){ sec[i] = 0; zero[i] = 0; }
    dir_dot(sec, 0, c);                       /* "." -> kendisi */
    dir_dot(sec + 32, 1, parent);             /* ".." -> ust dizin (kok: 0) */
    for(unsigned s = 0; s < csz; s++)
        if(v->wr(v->ctx, clus_sec(v, c) + s, 1, s == 0 ? sec : zero) != 0) return -1;
    return ent_plan_commit(v, parent, name, nlen, &p, 0x10, c, 0);
}

int fat_list(fat_vol *v, const char *path, fat_list_fn cb, void *ctx){
    fat_file d;
    unsigned char e[32];
    unsigned char lfn[FAT_NAME_BUF];
    char nm[FAT_NAME_BUF];
    int lfn_have = 0, lfn_ok = 0;
    unsigned char lfn_sum = 0;
    if(fat_open(v, path, &d) != 0 || !d.is_dir) return -1;
    for(unsigned i = 0; i < FAT_NAME_BUF; i++) lfn[i] = 0;
    for(unsigned idx = 0;; idx++){
        int r = dir_read(v, d.first, idx, e);
        if(r == 1) break;
        if(r < 0) return -1;
        if(e[0] == 0x00) break;
        if(e[0] == 0xE5){ lfn_have = 0; continue; }
        if(e[11] == FAT_ATTR_LFN){ lfn_step(lfn, &lfn_have, &lfn_ok, &lfn_sum, e); continue; }
        int named = lfn_have && lfn_ok && lfn_sum == lfn_checksum(e);
        lfn_have = 0;
        if(e[11] & 0x08) continue;            /* birim etiketi */
        if(e[0] == '.') continue;             /* "." ve ".." */
        if(named){
            int n = 0;
            while(n < FAT_NAME_BUF - 1 && lfn[n]){ nm[n] = (char)lfn[n]; n++; }
            nm[n] = 0;
        } else {
            fmt83(e, nm);
        }
        cb(ctx, nm, (e[11] & 0x10) ? 1u : 0u, rd32(e + 28));
    }
    return 0;
}
