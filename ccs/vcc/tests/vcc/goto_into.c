/* goto in every direction: out of loops, back, into loops, if branches,
   switch bodies and nested compounds (the wasm target has no goto: its
   dispatch loops, PLAN-wasm.md W4). */
#include <stdio.h>

static int into_loop(int n)
{
	int i = 0, s = 0;

	if (n > 2)
		goto mid;
	for (i = 0; i < n; i++) {
		s += 100;
mid:
		s += i;
	}
	return s;
}

static int into_if(int k)
{
	int s = 0;

	if (k == 1)
		goto then;
	if (k == 2)
		goto els;
	if (k > 10) {
then:
		s += 1;
	} else {
els:
		s += 2;
	}
	return s;
}

static int into_switch(int v)
{
	int s = 0;

	if (v < 0)
		goto inside;
	switch (v) {
	case 0:
		s = 10;
		goto common;
	case 1:
		s = 20;
inside:
		s += 1;
		/* fall through */
	case 2:
		s += 2;
common:
		s *= 3;
		break;
	default:
		s = -1;
	}
	return s;
}

static int back(int n)
{
	int s = 0;
again:
	s += n;
	if (--n > 0)
		goto again;
	{
		int k = 0;
inner:
		k++;
		if (k < 3)
			goto inner;
		s += k;
	}
	return s;
}

static int out_of_nest(int n)
{
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++)
			while (1) {
				if (i * j == 6)
					goto done;
				break;
			}
	return -1;
done:
	return n;
}

static int while_into(int n)
{
	int s = 0;

	goto body;
	while (n > 0) {
		s += 10;
body:
		s += n;
		n--;
	}
	return s;
}

static int do_into(int n)
{
	int s = 0;

	if (n)
		goto body;
	do {
		s += 7;
body:
		s += 1;
	} while (--n > 0);
	return s;
}

int main(void)
{
	printf("%d %d %d\n", into_loop(2), into_loop(5), into_loop(0));
	printf("%d %d %d %d\n", into_if(1), into_if(2), into_if(11), into_if(3));
	printf("%d %d %d %d %d\n", into_switch(-1), into_switch(0), into_switch(1), into_switch(2), into_switch(9));
	printf("%d %d\n", back(4), out_of_nest(5));
	printf("%d %d %d\n", while_into(3), do_into(3), do_into(0));
	return 0;
}
