/* parser recovery (D2.2): each error is reported once, parsing goes on with
   the next statement or declaration, and nothing after it is reported again */
struct S { int x; };
int a;
if (a) a = 1;
}
int g(void) { a = 1); return 1; }
int h(void) { int q[2]; q[0] = 1]; return q[1]; }
int t[3] = { 1, , 2 };
int f(int b)
{
	int i, n = 0;
	int v[4] = { 8, , 9, 10 };
	for (i = 0; i < b; i++
		n += v[i & 3];
	if (b != (n + 1)
		n = 2;
	else
		n = 3;
	for (i = 0; i < b @ 1; i++)
		n++;
	n = f(b;
	return n + v[0];
}
struct T { a, b; int c };
int k(void) { struct T t = { 1, 2, 3 }; return t.a + t.c; }
