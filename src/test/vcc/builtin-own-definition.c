/* A standard name written in a call means the library's function even where
 * the unit defines it, as clang at -O2; a sqrt the unit defines first is
 * called, and so is the unit's strlen when the literal only arrives through
 * inlining.  Nothing to see at -O0, where they are calls. */
typedef __SIZE_TYPE__ size_t;
int printf(const char *, ...);
double fmin(double a, double b) { return 77; }
size_t strlen(const char *s) { return 77; }
int strcmp(const char *a, const char *b) { return 77; }
void bzero(void *d, size_t n) { *(char *)d = 77; }
double sqrt(double x) { return 77; }
static size_t length(const char *s) { return strlen(s); }
int main(void)
{
    volatile int one = 1;
#ifdef __OPTIMIZE__
    char d[4] = {5, 5, 5, 5};
    bzero(d, 4);
    printf("%d %d %d %d %d %d\n", (int)fmin(one, 2), (int)strlen("abc"),
           (int)length("abc"), strcmp("a", "b"), d[0], (int)sqrt(one * 16.0));
#else
    printf("1 3 77 -1 0 77\n");
#endif
    return 0;
}
