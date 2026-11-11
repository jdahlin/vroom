/* -ftrivial-auto-var-init=zero: locals without an initializer start as zeros (the kernel) */
/* vcc-flags: -ftrivial-auto-var-init=zero -Wno-uninitialized */
/* vcc-xfail: arm64: -ftrivial-auto-var-init=zero is x86-64 only so far (TODO.md tier 1) */
int printf(const char *, ...);
struct S { int a; char b[13]; long c; };
__attribute__((noinline)) void dirty(void) { volatile char big[256]; for (int i = 0; i < 256; i++) big[i] = (char)0xa5; }
__attribute__((noinline)) int use(void) { int x; struct S s; char arr[7]; long sum = 0; sum += x; sum += s.a + s.b[12] + s.c; for (int i = 0; i < 7; i++) sum += arr[i]; return (int)sum; }
int main(void) { dirty(); printf("%d\n", use()); return 0; }
