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
    # PE ornegi: depodaki hazir derleme (MinGW gerekmez); yoksa derlemeyi dene
    prebuilt = os.path.join(ROOT, "app", "samples", "prebuilt", "PE_HELLO.EXE")
    if os.path.exists(prebuilt):
        with open(prebuilt, "rb") as f:
            files["PE_HELLO.EXE"] = f.read()
    else:
        try:
            with tempfile.TemporaryDirectory() as tmp:
                subprocess.run([sys.executable, os.path.join(ROOT, "app", "samples", "build_pe.py"), tmp],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                with open(os.path.join(tmp, "PE_HELLO.EXE"), "rb") as f:
                    files["PE_HELLO.EXE"] = f.read()
        except (OSError, subprocess.CalledProcessError):
            print("uyari: PE_HELLO.EXE bulunamadi ve derlenemedi; disk imajina eklenmedi")
    data = make_fat_images.build("fat16", files)
    with open(out, "wb") as f:
        f.write(data)
    print("yazildi:", out)


if __name__ == "__main__":
    main()
