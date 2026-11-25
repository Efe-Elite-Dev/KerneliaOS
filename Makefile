# KerneliaOS - build (build.sh ve CI ile ayni adimlar)
#   make        -> kernel.bin
#   make iso    -> kerneliaos.iso (grub-mkrescue gerekir)
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
	grub-mkrescue -o kerneliaos.iso iso
	rm -rf iso

run: iso
	./run.sh kerneliaos.iso

# Hata ayiklama: QEMU 1234 portunda gdb bekler ve duraklatilmis baslar.
#   Baska terminalde: gdb kernel.bin -ex 'target remote :1234' -ex 'break kernel_main' -ex continue
debug: iso
	qemu-system-i386 -cdrom kerneliaos.iso -m 256 -vga std -nic user,model=rtl8139 -serial stdio -s -S

test: 
	bash tools/qemu_test.sh
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_mem app/test/test_mem.c && ./test_mem
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_dns app/test/test_dns.c && ./test_dns
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_crypto app/test/test_crypto.c && ./test_crypto
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_x25519 app/test/test_x25519.c && ./test_x25519
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_tls app/test/test_tls.c && ./test_tls
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_rsa app/test/test_rsa.c && ./test_rsa
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_x509 app/test/test_x509.c && ./test_x509
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_ecdsa app/test/test_ecdsa.c && ./test_ecdsa
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_pss app/test/test_pss.c && ./test_pss
	bash tools/tls_interop.sh

clean:
	rm -rf boot.o kernel.o kernel.bin iso kerneliaos.iso

.PHONY: all iso run debug test clean
