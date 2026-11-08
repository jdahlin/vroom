/* _Atomic(T) has its own size and alignment, and atomic ops must not be
   plain loads/stores (TODO.md tier 1, "_Atomic dropped").  One core under
   qemu, so this only checks layout and single-threaded results; the
   threaded counter is in the host suite (T3). */
#include <stdio.h>

struct s3 { char a[3]; };
struct s12 { char a[12]; };

_Atomic struct s3 g3;
_Atomic int ai;

int main(void)
{
	int bad = 0;

	bad |= sizeof(_Atomic struct s3) != 4 || _Alignof(_Atomic struct s3) != 4;
	bad |= sizeof(_Atomic struct s12) != 16 || _Alignof(_Atomic struct s12) != 16;
	bad |= _Alignof(_Atomic long long) != 8 || _Alignof(_Atomic char) != 1;
	bad |= sizeof g3 != 4;
	ai = 5;
	ai++;
	ai += 3;
	ai *= 2;
	bad |= ai != 18;
	printf("%zu %zu %zu %d\n", sizeof(_Atomic struct s3), _Alignof(_Atomic struct s3),
	       sizeof(_Atomic struct s12), (int)ai);
	return bad;
}
