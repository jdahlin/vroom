/* int a[] = {..} after extern int a[10]: the composite type, 10 elements
   (the rest zero), not the initializer's 2. */
#include <stdio.h>

extern int a[10];
int a[] = { 12, 34 };
extern char s[8];
char s[] = "ab";

int main(void)
{
	int z = 0;

	for (int i = 2; i < 10; i++)
		z |= a[i];
	printf("%d %d %d %d %d\n", (int)sizeof a, (int)sizeof s, a[0], a[1], z | s[5]);
	return 0;
}
