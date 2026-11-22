/* pe.c icin host testi. Once ornegi uret:
     python3 app/samples/build_pe.py <klasor>
   Sonra: test_pe <klasor>/PE_HELLO.EXE
   Yukleme, farkli bir adrese (relocation ile) yapilir; cozucu sahte adresler verir.
   Calistirma yapilmaz: sadece yapinin dogrulugu sinanir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pe.h"

static int fails = 0;
static void check(const char *name, int ok){
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if(!ok) fails++;
}

/* cozucu kaydi: hangi import hangi sahte adresle cozuldu */
typedef struct { char dll[32], name[64]; unsigned addr; } rec_t;
static rec_t recs[16];
static int nrecs = 0;
static int dll_ok = 1;

static unsigned resolve(void *ctx, const char *dll, const char *name, unsigned ord){
    (void)ctx; (void)ord;
    if(nrecs >= 16) return 0;
    rec_t *r = &recs[nrecs];
    strncpy(r->dll, dll, sizeof r->dll - 1); r->dll[sizeof r->dll - 1] = 0;
    strncpy(r->name, name ? name : "", sizeof r->name - 1); r->name[sizeof r->name - 1] = 0;
    if(strcmp(dll, "KERNEL32.dll") != 0 && strcmp(dll, "kernel32.dll") != 0) dll_ok = 0;
    r->addr = 0x90000000u + (unsigned)nrecs * 0x10u;   /* sahte, ayirt edilebilir adres */
    nrecs++;
    return r->addr;
}

static unsigned char *read_file(const char *path, unsigned *n){
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long sz;
    if(!f) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)sz);
    if(!b || fread(b, 1, (size_t)sz, f) != (size_t)sz){ fclose(f); free(b); return NULL; }
    fclose(f);
    *n = (unsigned)sz;
    return b;
}

static unsigned rd32(const unsigned char *p){
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned rd16(const unsigned char *p){ return p[0] | ((unsigned)p[1] << 8); }

/* RVA'nin dosyadaki yeri (bolumlerden); bulunamazsa 0xFFFFFFFF */
static unsigned rva_to_file(const unsigned char *img, unsigned n, unsigned rva){
    unsigned lfa = rd32(img + 0x3C), opt = lfa + 24, optsz = rd16(img + lfa + 20);
    unsigned nsec = rd16(img + lfa + 6), secs = opt + optsz;
    for(unsigned i = 0; i < nsec; i++){
        const unsigned char *s = img + secs + i * 40;
        unsigned vsz = rd32(s + 8), va = rd32(s + 12), rsz = rd32(s + 16), rptr = rd32(s + 20);
        unsigned span = vsz > rsz ? vsz : rsz;
        if(rva >= va && rva - va < span && rva - va < rsz){
            unsigned off = rptr + (rva - va);
            return off < n ? off : 0xFFFFFFFFu;
        }
    }
    return 0xFFFFFFFFu;
}

int main(int argc, char **argv){
    unsigned n = 0, ofs;
    unsigned char *img;
    pe_info info;
    unsigned char *dst;
    unsigned load_addr = 0x00200000u, entry = 0, delta, nreloc_ok = 0;
    static unsigned char bad[4096];
    int r;

    if(argc < 2){ printf("kullanim: test_pe PE_HELLO.EXE\n"); return 2; }
    img = read_file(argv[1], &n);
    if(!img){ printf("FAIL dosya okunamadi: %s\n", argv[1]); return 1; }

    check("PE basligi okunur", pe_parse(img, n, &info) == PE_OK);
    check("bolum sayisi 5", info.nsections == 5);
    check("imaj tabani 0x400000", info.image_base == 0x400000u);

    dst = malloc(info.image_size);
    r = pe_load(img, n, dst, info.image_size, load_addr, resolve, 0, &info, &entry);
    check("yukleme basarili (farkli adres: relocation)", r == PE_OK);
    check("3 kernel32 importu cozuldu", info.nimports == 3 && nrecs == 3);
    check("import DLL adi KERNEL32.dll", dll_ok);
    {
        int have_wf = 0, have_gsh = 0, have_ep = 0;
        for(int i = 0; i < nrecs; i++){
            if(strcmp(recs[i].name, "WriteFile") == 0) have_wf = 1;
            if(strcmp(recs[i].name, "GetStdHandle") == 0) have_gsh = 1;
            if(strcmp(recs[i].name, "ExitProcess") == 0) have_ep = 1;
        }
        check("WriteFile, GetStdHandle, ExitProcess isimleri", have_wf && have_gsh && have_ep);
    }
    check("giris noktasi = yukleme adresi + RVA", entry == load_addr + info.entry_rva);
    check("relocation uygulandi", info.nrelocs > 0);

    /* bagimsiz relocation dogrulamasi: her hedef, dosyadaki degere (delta) eklenmis olmali */
    delta = load_addr - info.image_base;
    {
        unsigned lfa = rd32(img + 0x3C), opt = lfa + 24, ndirs = rd32(img + opt + 92);
        unsigned rva = 0, sz = 0;
        if(ndirs > 5){ rva = rd32(img + opt + 96 + 5 * 8); sz = rd32(img + opt + 96 + 5 * 8 + 4); }
        unsigned total = 0;
        for(unsigned p = rva; p + 8 <= rva + sz; ){
            unsigned page = rd32(img + rva_to_file(img, n, p));
            unsigned bsz = rd32(img + rva_to_file(img, n, p + 4));
            for(unsigned e = p + 8; e + 2 <= p + bsz; e += 2){
                unsigned ent = rd16(img + rva_to_file(img, n, e));
                if((ent >> 12) == 0) continue;
                unsigned target = page + (ent & 0xFFF);
                unsigned fo = rva_to_file(img, n, target);
                unsigned orig = rd32(img + fo);
                unsigned now = rd32(dst + target);
                total++;
                if(now == orig + delta) nreloc_ok++;
            }
            p += bsz;
        }
        check("her relocation dosyadaki degere delta eklenmis", total > 0 && nreloc_ok == total);
    }

    /* hata yollari */
    memcpy(bad, img, n < sizeof bad ? n : sizeof bad);
    bad[0] = 'X';
    check("MZ yoksa reddedilir", pe_parse(bad, n, &info) == PE_E_MZ);
    check("kisa dosya reddedilir", pe_parse(img, 0x20, &info) == PE_E_SHORT);
    ofs = rd32(img + 0x3C);
    memcpy(bad, img, n < sizeof bad ? n : sizeof bad);
    bad[ofs] = 'X';
    check("PE imzasi yoksa reddedilir", pe_parse(bad, n, &info) == PE_E_PE);
    memcpy(bad, img, n < sizeof bad ? n : sizeof bad);
    bad[ofs + 4] = 0x64; bad[ofs + 5] = 0x86;           /* x86-64 */
    check("PE32+ / x86-64 reddedilir", pe_parse(bad, n, &info) == PE_E_MACHINE);
    check("hedef tampon kucukse reddedilir",
          pe_load(img, n, dst, info.image_size - 1, load_addr, resolve, 0, &info, &entry) == PE_E_SIZE);
    check("bozuk dosya (kesilmis) yuklenmez",
          pe_parse(img, n / 2, &info) != PE_OK);

    free(dst);
    free(img);
    printf("\n%s (%d hata)\n", fails ? "BASARISIZ" : "TUMU GECTI", fails);
    return fails ? 1 : 0;
}
