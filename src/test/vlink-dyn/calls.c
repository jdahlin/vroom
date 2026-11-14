/* calls through the PLT, data through the GOT, function pointers to libc in
   writable data (R_X86_64_64 against an imported symbol) and in a relro table */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[] = { "alpha", "beta", "gamma" };	/* .data.rel.ro under PIE */
static size_t (*const lenf)(const char *) = strlen;
int (*putsf)(const char *) = puts;

static int cmp(const void *a, const void *b)	/* libc calls back into the executable */
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(void)
{
	const char *v[] = { names[2], names[0], names[1] };
	char *p = malloc(16);

	qsort(v, 3, sizeof *v, cmp);
	snprintf(p, 16, "%s-%zu", v[0], lenf(v[2]));
	putsf(p);
	printf("%s %s %s\n", v[0], v[1], v[2]);
	fputs("stdout via GOT\n", stdout);
	free(p);
	return 0;
}
