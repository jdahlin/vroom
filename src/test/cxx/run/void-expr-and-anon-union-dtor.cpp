// void() as an expression (libc++'s for (__pp = __cp, void(), __cp = __cp->__next_; ...)), and the destructor of a
// class with an anonymous union member of class type (__hash_node's union { value_type __value_; }): the union's
// members are not destroyed implicitly
inline void *operator new(unsigned long, void *p) noexcept { return p; }
static int dtors;
struct V { int x; V(int x) : x(x) {} ~V() { dtors++; } };
struct Node {
	int hash;
	union { V v; };
	Node(int h) : hash(h) { new (&v) V(h * 2); }
	~Node() {}
};
int main()
{
	int a = 0, b = 0;
	for (a = 1, void(), b = 2; a < 3; a++)
		b += a;
	{
		Node n(5);
		b += n.v.x;
	}
	return a == 3 && b == 5 + 10 && dtors == 0 ? 0 : 1;
}
