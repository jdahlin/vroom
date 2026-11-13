/* Executed through a synthetic hardware frame, never through a C call. */
struct frame { unsigned long ip, cs, flags, sp, ss; };
unsigned long got[16];
unsigned long returned_flags;
int failures, visits;
unsigned char saved_fp[512] __attribute__((aligned(16)));
unsigned char expected_fp[512] __attribute__((aligned(16)));
unsigned char actual_fp[512] __attribute__((aligned(16)));
#ifndef GREGS
volatile double sum = 1.25, delta = 2.5;
volatile long double wide = 1.5L, add = 2.25L;
#endif
static int fp_equal(void) {
    for (int i = 0; i < 416; i++) {
        /* Reserved bytes in FXSAVE's x87 slots have unspecified contents. */
        if (i >= 32 && i < 160 && (i - 32) % 16 >= 10) continue;
        if (expected_fp[i] != actual_fp[i]) return 0;
    }
    return 1;
}
extern void invoke0(void), invoke1(void);
__attribute__((noinline)) static void disturb(unsigned long *p) {
    *p = 123;
    __asm__ volatile("mov $999, %%rax; mov $998, %%r10; mov $997, %%r15" ::: "rax", "r10", "r15");
}
__attribute__((noinline)) static void check(struct frame *p) {
    unsigned long flags;
    unsigned long aligned __attribute__((aligned(64)));
    __asm__ volatile(".byte 0x9c; popq %0" : "=r"(flags));
    if (flags & 1024) failures++;
    if (!(p->flags & 1024) || (p->cs & 3) != 3 || (p->ss & 3) != 3 || !p->ip || !p->sp) failures++;
    disturb(&aligned);
    if (aligned != 123 || ((unsigned long)&aligned & 63)) failures++;
#ifndef GREGS
    sum += delta;
    wide += add;
#endif
    visits++;
}
__attribute__((interrupt)) void handler0(struct frame *p) { check(p); }
__attribute__((interrupt)) void handler1(struct frame *p, unsigned long error) {
    if (error != 0x12345678) failures++;
    check(p);
}
int main(void) {
    invoke0();
    for (int i=0;i<16;i++) if (i != 4 && got[i] != (unsigned long)i+100) return 10+i;
    if (!(returned_flags & 1024)) return 30;
    if (!fp_equal()) return 61;
    invoke1();
    for (int i=0;i<16;i++) if (i != 4 && got[i] != (unsigned long)i+100) return 40+i;
    if (!(returned_flags & 1024)) return 60;
    if (!fp_equal()) return 62;
#ifndef GREGS
    if (sum != 6.25 || wide != 6.0L) return 63;
#endif
    return failures || visits != 2;
}
