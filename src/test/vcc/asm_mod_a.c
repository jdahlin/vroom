/* asm %a of a symbol's address: sym+off(%rip), as clang (the kernel's
   static_cpu_has: testb %[bitnum], %a[cap_byte]). */
#include <stdio.h>

struct caps { unsigned char b[16]; } boot = { { 0, 0, 0, 0, 0, 0x20 } };

static int has(void)
{
#ifdef __x86_64__
	asm goto("testb $0x20, %a0\n\tjnz %l[yes]" :: "i"(&boot.b[5]) :: yes);
	return 0;
yes:
	return 1;
#else
	return (boot.b[5] & 0x20) != 0;
#endif
}

int main(void)
{
	printf("%d\n", has());
	boot.b[5] = 0;
	printf("%d\n", has());
	return 0;
}
