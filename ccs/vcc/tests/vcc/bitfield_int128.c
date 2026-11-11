/* vcc-xfail-wasm: TODO.md wasm: wide integer bitfields: extraction, signed extension and masked stores are not lowered. */
typedef unsigned __int128 U;
typedef __int128 S;
struct Base { U value : 65; };
static __attribute__((noinline)) int truth(struct Base *p, int y)
{
    return y && p->value;
}
#define CASE(W, P) \
static __attribute__((noinline)) int test_##W(void) { \
    struct __attribute__((packed)) { unsigned lead:P; U value:W; unsigned tail:7; } u; \
    struct __attribute__((packed)) { unsigned lead:P; S value:W; unsigned tail:7; } s; \
    U mask = ~(U)0 >> (128-W); \
    u.lead = 1; u.tail = 91; u.value = (U)1 << (W-1); \
    if (u.value != (U)1 << (W-1) || u.lead != 1 || u.tail != 91) return 1; \
    u.value = mask; \
    if (u.value != mask || u.lead != 1 || u.tail != 91) return 2; \
    if ((u.value = mask + 5) != 4 || u.value != 4) return 3; \
    s.lead = 1; s.tail = 83; s.value = -3; \
    if (s.value != -3 || s.lead != 1 || s.tail != 83) return 4; \
    if (s.value++ != -3 || s.value != -2) return 5; \
    if ((s.value += 5) != 3 || s.value != 3 || s.lead != 1 || s.tail != 83) return 6; \
    return 0; \
}
CASE(63, 7)
CASE(64, 1)
CASE(65, 1)
CASE(79, 7)
CASE(127, 3)
CASE(128, 7)
#if defined(__linux__) && defined(__x86_64__)
extern void *mmap(void *, unsigned long, int, int, int, long);
extern int mprotect(void *, unsigned long, int);
extern int munmap(void *, unsigned long);
struct __attribute__((packed)) Last { unsigned lead:7; U value:128; };
static __attribute__((noinline)) int access_last(struct Last *p, U value)
{
    p->value = value;
    return p->value == value && p->lead == 85;
}
static int guard_page(void)
{
    unsigned char *mem = mmap(0, 8192, 3, 0x22, -1, 0);
    if (mem == (void *)-1) return 1;
    if (mprotect(mem + 4096, 4096, 0)) return 2;
    struct Last *p = (struct Last *)(mem + 4096 - sizeof(struct Last));
    p->lead = 85;
    int ok = access_last(p, ((U)1 << 127) | 19);
    munmap(mem, 8192);
    return !ok;
}
#else
static int guard_page(void) { return 0; }
#endif
int main(void)
{
    if (guard_page()) return 80;
    struct Base b = { (U)1 << 64 };
    if (truth(&b, 0) || !truth(&b, 1)) return 10;
    b.value = 0;
    if (truth(&b, 1)) return 11;
    if (test_63()) return 60 + test_63();
    if (test_64()) return 70 + test_64();
    if (test_65()) return 20 + test_65();
    if (test_79()) return 30 + test_79();
    if (test_127()) return 40 + test_127();
    if (test_128()) return 50 + test_128();
    return 0;
}
