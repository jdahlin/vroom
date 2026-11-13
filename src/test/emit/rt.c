/* test runtime (always built by clang): UART output and the few library calls */
typedef unsigned long size_t;
static volatile unsigned int *const uart = (unsigned int *)0x09000000;
void putch(int c) { *uart = (unsigned)c; }
void puts_(const char *s) { while (*s) putch(*s++); }
void hex(unsigned long v) { puts_("0x"); for (int i = 60; i >= 0; i -= 4) putch("0123456789abcdef"[(v >> i) & 15]); putch('\n'); }
void out(const char *name, unsigned long v) { puts_(name); putch(' '); hex(v); }
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = (char)c; return d; }
void *memmove(void *d, const void *s, size_t n) { char *a = d; const char *b = s; if (a < b) while (n--) *a++ = *b++; else while (n--) a[n] = b[n]; return d; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int main(void) { extern void tests(void); tests(); puts_("done\n"); return 0; }
