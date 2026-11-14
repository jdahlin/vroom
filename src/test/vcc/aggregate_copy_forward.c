/* A sparse aggregate copied back over its inputs, and a copy after a call
 * which can modify its source. The packed destination may be unaligned. */
struct record {
	unsigned char kind, reg;
	unsigned long words[3];
};

struct __attribute__((packed)) packet {
	unsigned char guard;
	struct record value;
	unsigned char end;
};

static __attribute__((noinline)) void replace(struct packet *p)
{
	p->value = (struct record){ .kind = p->value.reg, .words = { p->value.words[2] } };
}

static __attribute__((noinline)) void mutate(struct record *r)
{
	r->words[1] = r->kind + 9;
}

static __attribute__((noinline)) struct record make(int x)
{
	struct record r = { .kind = x };
	if (x & 1)
		mutate(&r);
	return r;
}

int main(void)
{
	struct packet p = { 91, { 3, 7, { 22, 23, 24 } }, 92 };
	replace(&p);
	if (p.guard != 91 || p.end != 92 || p.value.kind != 7 || p.value.reg ||
	    p.value.words[0] != 24 || p.value.words[1] || p.value.words[2])
		return 1;
	return make(3).words[1] != 12 || make(4).words[1] != 0;
}
