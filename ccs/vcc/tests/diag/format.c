/* D5.1: -Wformat: printf/scanf format strings against their arguments, for the C library's
   functions and for __attribute__((format)); fix-its to the specification that fits, the
   argument counts, '*' amounts, invalid conversions (escapes located), flags, -Wformat-security */
typedef struct FILE FILE;
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int scanf(const char *, ...);
void report(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void show(long count, unsigned width, const char *name, double ratio, void *where, short *small, int n, FILE *out) {
	printf("%d items\n", count);
	printf("%s: %d\n", name);
	printf("%s\n", name, n);
	printf("%*s|\n", count, name);
	printf("%q\n", n);
	printf("100%\n");
	printf("%#s %+u\n", name, width);
	printf("%d %f %p\n", ratio, n, n);
	fprintf(out, "%lu\n", width);
	scanf("%d", small);
	report(1, "%s\n", n);
	report(2, n ? "%s:%d" : "%s", name, n);
	printf(name);
	printf("%1$d %d\n", n, n);
	printf("%zu %ld %hhd %c\n", sizeof(int), count, n, n);
}
