#!/usr/bin/env python3
"""Ornek .wind programlarini derler: hello.c -> HELLO.WIN

Kullanim: python3 app/samples/build_samples.py <cikti_klasoru>
Gerekenler: gcc (-m32 destekli), ld, objcopy, readelf.

Kod konum-bagimsiz olmak zorunda: derlenen nesnede yeniden konumlandirma
kaydi olmamali (mutlak adres yok). Varsa betik durur."""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MEM_SIZE = 4096


def run(args):
    subprocess.run(args, check=True)


def build_wind(src, out_path, workdir):
    obj = os.path.join(workdir, "prog.o")
    elf = os.path.join(workdir, "prog.elf")
    binf = os.path.join(workdir, "prog.bin")
    run(["gcc", "-m32", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
         "-fno-pic", "-fno-pie", "-O2", "-Wall", "-c", src, "-o", obj])
    rel = subprocess.run(["readelf", "-rW", obj], capture_output=True, text=True, check=True).stdout
    # Yalnizca kodu/veriyi etkileyen yeniden konumlandirmalar sorun (.eh_frame sorun degil)
    for m in re.finditer(r"Relocation section '([^']+)' at offset \S+ contains (\d+) entr", rel):
        name, count = m.group(1), int(m.group(2))
        if count and any(k in name for k in (".text", ".rodata", ".data", ".bss")):
            sys.exit("HATA: program konuma bagimli (" + name + " icinde yeniden konumlandirma)")
    run(["ld", "-m", "elf_i386", "-Ttext=0", "-e", "entry", "-o", elf, obj])
    run(["objcopy", "-O", "binary", "-j", ".text.entry", "-j", ".text", elf, binf])
    syms = subprocess.run(["nm", elf], capture_output=True, text=True, check=True).stdout
    entry = None
    for line in syms.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "entry":
            entry = int(parts[0], 16)
    if entry is None:
        sys.exit("HATA: entry sembolu yok")
    with open(binf, "rb") as f:
        code = f.read()
    if not code or entry >= len(code):
        sys.exit("HATA: giris noktasi kodun disinda")
    header = b"WIND" + struct.pack("<HHIII", 1, 0, entry, len(code), MEM_SIZE)
    with open(out_path, "wb") as f:
        f.write(header + code)
    return out_path


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out_dir, exist_ok=True)
    workdir = os.path.join(out_dir, ".build")
    os.makedirs(workdir, exist_ok=True)
    path = build_wind(os.path.join(HERE, "hello.c"), os.path.join(out_dir, "HELLO.WIN"), workdir)
    print("yazildi:", path)


if __name__ == "__main__":
    main()
