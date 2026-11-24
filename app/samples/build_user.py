#!/usr/bin/env python3
"""Kullanici alani ornek programlarini derler (i686-w64-mingw32-gcc gerekir).

Kullanim: python3 app/samples/build_user.py <cikti_klasoru>
Uretilenler: ULIB_HELLO.EXE (mini libc), FAULT_TEST.EXE (koruma testi).
CRT yok (-nostdlib); giris noktasi _start (MinGW'de sembol __start)."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ULIB = os.path.join(HERE, "..", "ulib", "ulib.c")
PROGS = [("ulib_hello.c", "ULIB_HELLO.EXE", True), ("fault_test.c", "FAULT_TEST.EXE", True)]


def main():
    if len(sys.argv) < 2:
        sys.exit("kullanim: build_user.py <cikti_klasoru>")
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    for src, exe, uses_ulib in PROGS:
        out = os.path.join(out_dir, exe)
        cmd = ["i686-w64-mingw32-gcc", "-O2", "-Wall", "-fno-pic", "-fno-stack-protector",
               "-fno-builtin", "-fno-tree-loop-distribute-patterns", "-Wno-unused-function",
               "-nostdlib", "-nostartfiles", "-Wl,-e,__start", "-Wl,--subsystem,console",
               "-o", out, os.path.join(HERE, src)]
        if uses_ulib:
            cmd.append(ULIB)
        subprocess.run(cmd, check=True)
        print("yazildi:", out)


if __name__ == "__main__":
    main()
