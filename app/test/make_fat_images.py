#!/usr/bin/env python3
"""app/fat.c icin test imajlari uretir (fat16.img ve fat12.img).

Kullanim: python3 app/test/make_fat_images.py <cikti_klasoru>

Icerik (her iki imajda):
  HELLO.TXT          5000 bayt (birden fazla kume), desen: byte i = (i*7) & 0xFF
  SUB/INNER.TXT      "ICERIDEN"
  SUB/DEEP/X.TXT     "DERIN!"
fat12.img icin HELLO.TXT 3000 bayt.
"""
import os
import struct
import sys


def pattern(n):
    return bytes((i * 7) & 0xFF for i in range(n))


def name83(name):
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    return (base.upper().ljust(8)[:8] + ext.upper().ljust(3)[:3]).encode("ascii")


def entry(name, attr, first, size):
    if name in (".", ".."):
        raw = (name + " " * (11 - len(name))).encode("ascii")  # "." ve ".." ozel girisler
    else:
        raw = name83(name)
    return struct.pack("<11sB10sHHHI", raw, attr, b"\0" * 10, 0, 0, first, size)


def lfn_checksum(short11):
    s = 0
    for c in short11:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


def needs_lfn(name):
    """8.3 disinda kalan ad (uzun, kucuk harf ya da bosluklu) LFN ister"""
    base, ext = name.rsplit(".", 1) if "." in name else (name, "")
    return name != name.upper() or len(base) > 8 or len(ext) > 3 or " " in name


def short_alias(name):
    """Uzun ad icin 8.3 takma ad: ILK6~1.EXT (sadece test icin, sade bir kural)"""
    base, ext = name.rsplit(".", 1) if "." in name else (name, "")
    clean = "".join(c for c in base.upper() if c.isalnum())[:6] + "~1"
    cext = "".join(c for c in ext.upper() if c.isalnum())[:3]
    return clean + ("." + cext if cext else "")


def lfn_entries(longname, short11, bad_sum=False):
    """Uzun ad icin LFN girisleri; diskte once en yuksek sira (0x40 bayragi) gelir."""
    units = [ord(c) for c in longname] + [0]           # 0x0000 sonlandirici
    while len(units) % 13:
        units.append(0xFFFF)                           # sonrasi 0xFFFF dolgu
    n = len(units) // 13
    csum = lfn_checksum(short11)
    if bad_sum:
        csum = (csum + 1) & 0xFF                       # bayat zincir simulasyonu
    out = b""
    for seq in range(n, 0, -1):
        chunk = units[(seq - 1) * 13: seq * 13]
        first = seq | (0x40 if seq == n else 0)
        raw = struct.pack("<B5H", first, *chunk[0:5])
        raw += struct.pack("<BBB6H", 0x0F, 0, csum, *chunk[5:11])
        raw += struct.pack("<H2H", 0, *chunk[11:13])
        out += raw
    return out


def build(kind, extra=None, bad_sum=(), lfn_dir=False):
    """extra: {"AD.UZ": bytes} -> kok dizine ek dosyalar.
    Uzun adli (LFN) dosyalar icin 8.3 takma ad otomatik uretilir.
    bad_sum: LFN sagla toplami bilerek yanlis yazilacak uzun adlar.
    lfn_dir: kok dizinde "Long Dir Name/Inner Long File.txt" (uzun adli dizin)."""
    if kind == "fat16":
        bps, spc, reserved, nfats, root_ent, total, media, fmt = 512, 4, 1, 2, 512, 32768, 0xF8, 16
    else:
        bps, spc, reserved, nfats, root_ent, total, media, fmt = 512, 1, 1, 2, 224, 2880, 0xF0, 12
    rootsec = (root_ent * 32 + bps - 1) // bps
    fatsz = 1
    while True:
        d = total - reserved - nfats * fatsz - rootsec
        clusters = d // spc
        need = ((clusters + 2) * 3 + 1) // 2 if fmt == 12 else (clusters + 2) * 2
        if need <= fatsz * bps:
            break
        fatsz += 1
    if fmt == 12:
        assert clusters < 4085, clusters
    else:
        assert clusters >= 4085, clusters

    img = bytearray(total * bps)
    fat = bytearray(fatsz * bps)
    eoc = 0xFFF if fmt == 12 else 0xFFFF

    def set_fat(c, val):
        if fmt == 12:
            off = c + c // 2
            if c % 2 == 0:
                fat[off] = val & 0xFF
                fat[off + 1] = (fat[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
            else:
                fat[off] = (fat[off] & 0x0F) | ((val & 0x0F) << 4)
                fat[off + 1] = (val >> 4) & 0xFF
        else:
            struct.pack_into("<H", fat, c * 2, val)

    set_fat(0, (0xF00 | media) if fmt == 12 else (0xFF00 | media))
    set_fat(1, eoc)

    data_start = reserved + nfats * fatsz + rootsec
    csz = spc * bps
    next_free = [2]

    def alloc(nbytes):
        n = max(1, (nbytes + csz - 1) // csz)
        cs = list(range(next_free[0], next_free[0] + n))
        next_free[0] += n
        for i, c in enumerate(cs):
            set_fat(c, cs[i + 1] if i + 1 < n else eoc)
        return cs

    def write_clusters(cs, data):
        for i, c in enumerate(cs):
            chunk = data[i * csz:(i + 1) * csz]
            off = (data_start + (c - 2) * spc) * bps
            img[off:off + len(chunk)] = chunk

    hello_size = 5000 if fmt == 16 else 3000
    hello = pattern(hello_size)
    cs_hello = alloc(len(hello))
    write_clusters(cs_hello, hello)

    cs_inner = alloc(8)
    write_clusters(cs_inner, b"ICERIDEN")
    cs_x = alloc(6)
    write_clusters(cs_x, b"DERIN!")

    cs_sub = alloc(csz)                    # ".." girisleri icin once ayrilir
    cs_deep = alloc(csz)
    deep = entry(".", 0x10, cs_deep[0], 0) + entry("..", 0x10, cs_sub[0], 0) + \
        entry("X.TXT", 0x20, cs_x[0], 6)
    write_clusters(cs_deep, deep.ljust(csz, b"\0"))

    sub = entry(".", 0x10, cs_sub[0], 0) + entry("..", 0x10, 0, 0) + \
        entry("INNER.TXT", 0x20, cs_inner[0], 8) + \
        entry("DEEP", 0x10, cs_deep[0], 0)
    write_clusters(cs_sub, sub.ljust(csz, b"\0"))

    root = entry("HELLO.TXT", 0x20, cs_hello[0], len(hello)) + \
        entry("SUB", 0x10, cs_sub[0], 0)
    if lfn_dir:
        # Uzun adli dizin: kendi "." ve ".." girisleri ve LFN'li bir dosya
        cs_ld = alloc(csz)
        cs_li = alloc(8)
        write_clusters(cs_li, b"ICI UZUN")
        li_short = name83("INNERL~1.TXT")
        ld = entry(".", 0x10, cs_ld[0], 0) + entry("..", 0x10, 0, 0) + \
            lfn_entries("Inner Long File.txt", li_short) + \
            struct.pack("<11sB10sHHHI", li_short, 0x20, b"\0" * 10, 0, 0, cs_li[0], 8)
        write_clusters(cs_ld, ld.ljust(csz, b"\0"))
        ld_short = name83("LONGDI~1")
        root += lfn_entries("Long Dir Name", ld_short) + \
            struct.pack("<11sB10sHHHI", ld_short, 0x10, b"\0" * 10, 0, 0, cs_ld[0], 0)
    for name, data in (extra or {}).items():
        cs_x = alloc(len(data))
        write_clusters(cs_x, data)
        if needs_lfn(name):
            alias = short_alias(name)
            short = name83(alias)
            root += lfn_entries(name, short, bad_sum=name in bad_sum)
            root += struct.pack("<11sB10sHHHI", short, 0x20, b"\0" * 10, 0, 0, cs_x[0], len(data))
        else:
            root += entry(name, 0x20, cs_x[0], len(data))
    root_off = (reserved + nfats * fatsz) * bps
    img[root_off:root_off + len(root)] = root

    for k in range(nfats):
        off = (reserved + k * fatsz) * bps
        img[off:off + len(fat)] = fat

    # Boot sektoru (BPB): fat.c bu alanlari okur
    bs = bytearray(bps)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"KERNELIAOS  "
    struct.pack_into("<H", bs, 11, bps)
    bs[13] = spc
    struct.pack_into("<H", bs, 14, reserved)
    bs[16] = nfats
    struct.pack_into("<H", bs, 17, root_ent)
    if total < 65536:
        struct.pack_into("<H", bs, 19, total)
    else:
        struct.pack_into("<I", bs, 32, total)
    bs[21] = media
    struct.pack_into("<H", bs, 22, fatsz)
    struct.pack_into("<H", bs, 24, 63)
    struct.pack_into("<H", bs, 26, 255)
    bs[38] = 0x29                                 # genisletilmis BPB imzasi
    bs[39:43] = b"\x12\x34\x56\x78"               # birim seri numarasi
    bs[43:54] = b"NO NAME    "                    # birim etiketi
    bs[54:62] = b"FAT16   " if fmt == 16 else b"FAT12   "
    bs[510] = 0x55
    bs[511] = 0xAA
    img[0:bps] = bs

    return bytes(img)


# Test icin kok dizin LFN dosyalari (test_fat.c bunlari bekler)
LFN_EXTRA = {
    "HELLO.WIND": pattern(1500),                         # 8.3'e sigmayan uzanti
    "Long Name Sample.txt": b"UZUN AD",                  # 20 karakter, 2 LFN girisi
    "a very long file name for testing.dat": b"LFN3",    # 36 karakter, 3 LFN girisi
    "Stale Name.txt": b"YANLIS",                         # sagla toplami bozuk: uzun ad bulunamamali,
                                                         # ama kisa ad STALEN~1.TXT acilabilmeli
    "A" * 120 + ".txt": b"LONG120",                      # 120 karakter (eski 80 sinirinin ustu)
    "C" * 255: b"LONG255",                               # en uzun ad (255 karakter, 20 LFN girisi)
}
LFN_BAD_SUM = ("Stale Name.txt",)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for kind in ("fat16", "fat12"):
        path = os.path.join(out, kind + ".img")
        with open(path, "wb") as f:
            f.write(build(kind, LFN_EXTRA, bad_sum=LFN_BAD_SUM, lfn_dir=True))
        print("yazildi:", path)


if __name__ == "__main__":
    main()
