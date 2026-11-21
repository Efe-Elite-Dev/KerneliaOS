# WindOS - build (build.sh ve CI ile ayni adimlar)
#   make        -> kernel.bin
#   make iso    -> windos.iso (grub-mkrescue gerekir)
#   make clean

CC      = gcc
AS      = nasm
LD      = ld
CFLAGS  = -m32 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -O2 -Wall
LDFLAGS = -m elf_i386 -z noexecstack -T linker.ld

all: kernel.bin

boot.o: boot.asm
	$(AS) -f elf32 boot.asm -o boot.o

kernel.o: kernel.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

kernel.bin: boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) boot.o kernel.o -o kernel.bin

iso: kernel.bin grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o windos.iso iso
	rm -rf iso

run: iso
	./run.sh windos.iso

clean:
	rm -rf boot.o kernel.o kernel.bin iso windos.iso

.PHONY: all iso run clean
