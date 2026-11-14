/* Only three pages are touched; large displacements must not be truncated. */
#if defined(__linux__) && defined(__x86_64__)
extern void *mmap(void *, unsigned long, int, int, int, long);
extern int munmap(void *, unsigned long);
static __attribute__((noinline)) unsigned char *forward(unsigned char *p)
{
    return p + 0x80000000UL;
}
static __attribute__((noinline)) unsigned char *backward(unsigned char *p)
{
    return p - 0x100000000UL;
}
static __attribute__((noinline)) int memory(unsigned char *p)
{
    p[0x80000000UL] = 17;
    p[0x100000000UL] = 23;
    return p[0x80000000UL] + p[0x100000000UL];
}
int main(void)
{
    unsigned long size = 0x100002000UL;
    unsigned char *p = mmap(0, size, 3, 0x4022, -1, 0);
    if (p == (void *)-1) return 1;
    if (forward(p) - p != 0x80000000UL) return 2;
    if (backward(p + 0x100000000UL) != p) return 3;
    if (memory(p) != 40) return 4;
    return munmap(p, size) != 0;
}
#else
int main(void) { return 0; }
#endif
