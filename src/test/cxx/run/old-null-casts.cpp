// nullptr, reinterpret_cast and the math builtins
struct Node { int v; Node *next; };

static Node *find(Node *n, int v)
{
	while (n != nullptr) {
		if (n->v == v)
			return n;
		n = n->next;
	}
	return nullptr;
}

static unsigned long bits(const int *p) { return reinterpret_cast<unsigned long>(p); }
static const char *bytes(const int *p) { return reinterpret_cast<const char *>(p); }
static double root(double x) { return __builtin_sqrt(x); }
static float absf(float x) { return __builtin_fabsf(x); }
static int is_null(const char *p) { return p == nullptr; }

int main()
{
	Node a, b, c;
	int x = 0x01020304;

	a.v = 1, a.next = &b;
	b.v = 2, b.next = &c;
	c.v = 3, c.next = nullptr;

	if (find(&a, 3) != &c || find(&a, 9) != nullptr)
		return 1;
	if (bits(&x) != (unsigned long)&x)
		return 2;
	if (bytes(&x)[0] != 4)
		return 3;
	if (root(81.0) != 9.0 || absf(-2.5f) != 2.5f)
		return 4;
	if (!is_null(nullptr) || is_null("x"))
		return 5;
	return 0;
}
