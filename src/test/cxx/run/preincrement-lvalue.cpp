// ++x and --x are lvalues in C++: bound to a reference, assigned through, their address taken (libc++'s
// move_backward: *--__result = __iter_move(--__last_iter)); evaluated once
int &id(int &r) { return r; }
int *step(int *&p) { return p; }
int main()
{
	int a[4] = { 1, 2, 3, 4 }, *p = a + 3, *q = a, i = 5, n = 0;
	int &r = ++i;
	r += 10;
	--id(i) = 40;
	*--p = 9;
	int *s = step(++q);
	int *arr[2] = { a, a + 1 };
	++*(n++, &arr[0]);
	return i == 40 && a[2] == 9 && s == a + 1 && q == a + 1 && arr[0] == a + 1 && n == 1 ? 0 : 1;
}
