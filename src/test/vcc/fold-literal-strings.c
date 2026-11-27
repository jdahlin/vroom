/* strlen and strcpy of a literal that arrives through inlining: folded to the
 * length and to a copy, with the same values (offsets, embedded NULs, the
 * terminator, nothing written past it). */
__SIZE_TYPE__ strlen(const char *);
char *strcpy(char *, const char *);
int printf(const char *, ...);
static __SIZE_TYPE__ length(const char *s) { return strlen(s); }
static void copy(char *d, const char *s) { strcpy(d, s); }
static char *copy_keep(char *d, const char *s) { return strcpy(d, s); }
int main(void)
{
    char a[32], b[16];
    for (int i = 0; i < 32; i++)
        a[i] = '#';
    a[31] = 0;
    printf("%d %d %d %d %d\n", (int)length("abc"), (int)length(""),
           (int)length("abcdef" + 2), (int)length("ab\0cd"),
           (int)length("ab\0cd" + 3));
    copy(a, "hello");
    printf("%s %d %c%c\n", a, a[5], a[6], a[30]);
    copy(a, "");
    printf("%d %c\n", a[0], a[1]);
    printf("%s\n", copy_keep(b, "kept result"));
    copy(a, "longer than sixteen bytes");
    printf("%s\n", a);
    return 0;
}
