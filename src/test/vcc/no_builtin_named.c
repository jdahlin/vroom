/* vcc-flags: -fno-builtin-abs -fno-builtin-sin -fno-builtin-strncmp */
/* Each named disable flag is independent; unrelated labs remains a builtin. */
extern void abort(void);
static int abs_calls, sin_calls, strncmp_calls, labs_calls;
__attribute__((noinline)) int abs(int x) { abs_calls++; return x < 0 ? -x : x; }
__attribute__((noinline)) long labs(long x) { labs_calls++; return x < 0 ? -x : x; }
__attribute__((noinline)) double sin(double x) { sin_calls++; return x; }
__attribute__((noinline)) int strncmp(const char *a, const char *b, __SIZE_TYPE__ n) {
 strncmp_calls++;
 while (n--) { unsigned char x=*a++, y=*b++; if (!x || x!=y) return x-y; }
 return 0;
}
int main(void) {
 if (abs(0) || labs(0) || sin(0.0) || strncmp("a", "b", 0)) abort();
 if (abs_calls!=1 || sin_calls!=1 || strncmp_calls!=1 || labs_calls!=0) abort();
 return 0;
}
