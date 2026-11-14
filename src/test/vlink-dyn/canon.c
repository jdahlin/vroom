/* a libc function's address: the executable's (canonical PLT under -fno-pic,
   the GOT under PIE) equals libc's own view of it */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

void *fp_data = (void *)&abort;			/* writable: R_X86_64_64 */

int main(void)
{
	void *code = (void *)&abort;		/* code: PC32/32S or GOTPCREL */
	void *libc = dlsym(RTLD_DEFAULT, "abort");

	printf("code==data %d\n", code == fp_data);
	printf("code==dlsym %d\n", code == libc);
	return 0;
}
