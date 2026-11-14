#define NI __attribute__((noinline))
typedef unsigned long long word;
struct token { word pointer; unsigned length, pad; };
struct big { word a, b, c; };

NI void token_copy(struct token *dst, word p, unsigned n)
{
	struct token t = {0};
	t.pointer = p;
	t.length = n;
	*dst = t;
}

NI word overlap(unsigned n, unsigned char byte)
{
	word w = 0;
	__builtin_memcpy(&w, &n, sizeof n);
	((unsigned char *)&w)[1] = byte;
	return w;
}

NI struct big produce(word n)
{
	return (struct big){n + 1, n + 2, n + 3};
}

NI struct big wrapper(word n)
{
	return produce(n);
}

NI struct big adjusted(word n)
{
	struct big r = produce(n);
	r.b += r.a;
	return r;
}

NI struct big read_old(const struct big *p)
{
	return (struct big){p->c, p->a, p->b};
}

NI int rotate(void)
{
	struct big r = {1, 2, 3};
	r = read_old(&r);
	return r.a == 3 && r.b == 1 && r.c == 2;
}

int main(void)
{
	struct token t;
	word n = 0xfedcba9876543210ULL;
	token_copy(&t, n, 0x89abcdefu);
	if (t.pointer != n || t.length != 0x89abcdefu || t.pad)
		return 1;
	if (overlap(0xdeadbeefu, 0x12) != 0xdead12efu)
		return 2;
	struct big r = wrapper(n);
	if (r.a != n + 1 || r.b != n + 2 || r.c != n + 3)
		return 3;
	r = adjusted(7);
	return r.a != 8 || r.b != 17 || r.c != 10 || !rotate();
}
