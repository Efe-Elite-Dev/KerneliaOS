#!/usr/bin/env python3
"""QEMU icin WindOS disk imaji: FAT16, kokte HELLO.TXT, SUB/..., HELLO.WIND, PE_HELLO.EXE.

Kullanim: python3 app/test/make_disk.py <cikti_dosyasi.img>
(HELLO.WIN once app/samples/build_samples.py ile uretilir; betik bunu kendi
calisma klasorunde olusturur.)

QEMU'da: -hda <cikti_dosyasi.img>   (run.bat bunu otomatik ekler)"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import make_fat_images  # noqa: E402


def main():
    if len(sys.argv) < 2:
        sys.exit("kullanim: make_disk.py <cikti.img>")
    out = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, os.path.join(ROOT, "app", "samples", "build_samples.py"), tmp],
                       check=True, stdout=subprocess.DEVNULL)
        with open(os.path.join(tmp, "HELLO.WIN"), "rb") as f:
            wind = f.read()
    files = {"HELLO.WIND": wind}
    # Hazir derlemeler (depoda): PE_HELLO.EXE, ULIB_HELLO.EXE, FAULT_TEST.EXE
    # Eksik olan bir dosya icin uyari verilir; ornek derlemeler app/samples/build_*.py ile yeniden uretilir.
    prebuilt_dir = os.path.join(ROOT, "app", "samples", "prebuilt")
    for name in ("PE_HELLO.EXE", "ULIB_HELLO.EXE", "FAULT_TEST.EXE", "WIN_CONSOLE.EXE", "WIN_MISSING.EXE"):
        path = os.path.join(prebuilt_dir, name)
        if os.path.exists(path):
            with open(path, "rb") as f:
                files[name] = f.read()
        else:
            print("uyari: " + name + " app/samples/prebuilt altinda yok; disk imajina eklenmedi")
    data = make_fat_images.build("fat16", files)
    with open(out, "wb") as f:
        f.write(data)
    print("yazildi:", out)


if __name__ == "__main__":
    main()
