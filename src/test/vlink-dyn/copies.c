/* data defined in libc: copy relocations under -fno-pic (environ and its
   aliases share the copy libc writes), the GOT under PIE */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

extern char **environ;

int main(void)
{
	int n = 0;

	for (char **e = environ; e && *e; e++)
		n++;
	errno = 0;
	if (write(-1, "", 1) < 0 && errno == EBADF)
		puts("errno ok");
	printf("environ %s\n", n > 0 ? "set" : "empty");
	printf("stdin %s\n", stdin != stdout ? "distinct" : "same");
	return 0;
}
