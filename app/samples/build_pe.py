#!/usr/bin/env python3
"""pe_hello.exe uretir (i686-w64-mingw32-gcc gerekir).

Kullanim: python3 app/samples/build_pe.py <cikti_klasoru>

CRT baglanmaz (-nostdlib): yalnizca kernel32 import'lari olur, bu KerneliaOS'un
desteklediği alt kumeyle eslesir. Relocation tablosu (.reloc) korunur; boylece
yukleyicinin relocation yolu da test edilir."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) < 2:
        sys.exit("kullanim: build_pe.py <cikti_klasoru>")
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "PE_HELLO.EXE")
    cmd = ["i686-w64-mingw32-gcc", "-O2", "-Wall", "-nostdlib", "-nostartfiles",
           "-fno-stack-protector", "-Wl,-e,__start", "-Wl,--subsystem,console",
           "-o", out, os.path.join(HERE, "pe_hello.c"), "-lkernel32"]
    subprocess.run(cmd, check=True)
    print("yazildi:", out)


if __name__ == "__main__":
    main()
