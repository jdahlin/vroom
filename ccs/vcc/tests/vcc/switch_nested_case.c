/* case labels in nested statements (Duff's device, case inside if (0)) and
   labels in statement expressions: wasm goes there with its goto dispatch */
#include <stdio.h>
static void copy(char *to, const char *from, int count)
{
	int n = (count + 7) / 8;
	switch (count % 8) {
	case 0: do { *to++ = *from++;
	case 7: *to++ = *from++;
	case 6: *to++ = *from++;
	case 5: *to++ = *from++;
	case 4: *to++ = *from++;
	case 3: *to++ = *from++;
	case 2: *to++ = *from++;
	case 1: *to++ = *from++;
		} while (--n > 0);
	}
}
int f(int i)
{
	switch (i) {
		if (0) {
	default: return 7;
		case 42: return 1;
		}
	case 41: return 2;
	}
	return 0;
}
static int sx(int n)
{
	return ({ int i = 0; again: i++; if (i < n) goto again; i; });
}

int main(void)
{
	char b[40] = {0};
	for (int k = 0; k < 20; k++) {
		copy(b, "abcdefghijklmnopqrstuvwxyz", k);
		b[k] = 0;
		printf("%s|", b);
	}
	printf("\n%d %d %d %d\n", f(42), f(41), f(3), sx(5));
}
