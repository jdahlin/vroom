// std: c++20
// [expr.prim.id.dtor], [expr.ref]/3-4: explicit destructor calls p->~T(), obj.~X(), p->X::~X() and through a
// typedef or alias, in templates where T is a class or a scalar; a virtual destructor called through a base pointer
// (the final overrider) and, qualified, not; pseudo-destructors that only evaluate the object expression; a
// placement-new container as libc++'s and vcc's own owned_objects.
void *operator new(unsigned long, void *p) noexcept { return p; }

static int log[16];
static int logged;
static void note(int v) { log[logged++] = v; }

struct Plain { int v; ~Plain() { note(v); } };
struct Trivial { int v; };
struct Base { virtual ~Base() { note(1); } };
struct Derived : Base { ~Derived() { note(2); } };
struct Leaf : Derived { ~Leaf() { note(3); } };
struct Member { ~Member() { note(40); } };
struct Holder { Member m; ~Holder() { note(41); } };

typedef Plain PlainAlias;
using PlainUsing = Plain;
namespace N { struct Ns { ~Ns() { note(50); } }; }

template <class T> struct Owned
{
	unsigned long storage[(4 * sizeof(T) + 7) / 8];
	int count = 0;
	T *at(int i) { return reinterpret_cast<T *>(storage) + i; }
	T *push() { return new (at(count++)) T; }
	~Owned() { while (count) at(--count)->~T(); }
};

template <class T> void destroy(T *p) { p->~T(); }
template <class T> void destroy_qualified(T *p) { p->T::~T(); }
template <class T> void destroy_ref(T &r) { r.~T(); }

int steps;
int *advance(int *p) { ++steps; return p; }
typedef int I;
int *scalar_alias(int *p) { (advance(p))->~I(); advance(p)->I::~I(); return p; }

unsigned long buffer[(sizeof(Plain) + 7) / 8];
unsigned long leaf[(sizeof(Leaf) + 7) / 8];
unsigned long held[(sizeof(Holder) + 7) / 8];
unsigned long ns[(sizeof(N::Ns) + 7) / 8];

int main()
{
	int bad = 0;
	Plain *p = new (static_cast<void *>(buffer)) Plain{7};
	p->~Plain();
	bad |= logged != 1 || log[0] != 7;

	new (static_cast<void *>(buffer)) Plain{8};
	reinterpret_cast<Plain *>(buffer)->~PlainAlias();
	new (static_cast<void *>(buffer)) Plain{9};
	reinterpret_cast<Plain *>(buffer)->~PlainUsing();
	new (static_cast<void *>(buffer)) Plain{10};
	reinterpret_cast<Plain *>(buffer)->Plain::~Plain();
	new (static_cast<void *>(buffer)) Plain{11};
	destroy(reinterpret_cast<Plain *>(buffer));
	new (static_cast<void *>(buffer)) Plain{12};
	destroy_qualified(reinterpret_cast<Plain *>(buffer));
	new (static_cast<void *>(buffer)) Plain{13};
	destroy_ref(*reinterpret_cast<Plain *>(buffer));
	bad |= logged != 7 || log[1] != 8 || log[2] != 9 || log[3] != 10 || log[4] != 11 || log[5] != 12 || log[6] != 13;

	Plain object{14};
	object.~Plain();
	new (&object) Plain{15};
	bad |= logged != 8 || log[7] != 14;

	// virtual: the final overrider through a base pointer; qualified: only that class's
	logged = 0;
	Base *b = new (leaf) Leaf;
	b->~Base();
	bad |= logged != 3 || log[0] != 3 || log[1] != 2 || log[2] != 1;
	logged = 0;
	b = new (leaf) Leaf;
	b->Base::~Base();
	bad |= logged != 1 || log[0] != 1;
	logged = 0;
	Derived *d = new (leaf) Leaf;
	d->~Derived();
	bad |= logged != 3 || log[0] != 3;

	// a container of placement-new objects destroyed in reverse order, as owned_objects
	logged = 0;
	{
		Owned<Plain> owned;
		owned.push()->v = 21;
		owned.push()->v = 22;
		logged = 0;
	}
	bad |= logged != 2 || log[0] != 22 || log[1] != 21;

	// members and bases of a class destroyed by its own destructor
	logged = 0;
	new (held) Holder;
	reinterpret_cast<Holder *>(held)->~Holder();
	bad |= logged != 2 || log[0] != 41 || log[1] != 40;

	// a class in a namespace, qualified through the namespace
	logged = 0;
	N::Ns *n = new (ns) N::Ns;
	n->N::Ns::~Ns();
	new (ns) N::Ns;
	n->~Ns();
	bad |= logged != 2;

	// a trivial destructor: the object expression is evaluated, nothing is called
	Trivial t{1};
	Trivial *tp = &t;
	steps = 0;
	(tp + (steps++, 0))->~Trivial();
	t.~Trivial();
	bad |= steps != 1;

	// pseudo-destructors: evaluate the object expression only
	int x = 5;
	int *ip = &x;
	steps = 0;
	scalar_alias(ip);
	bad |= steps != 2;
	destroy(ip);
	destroy_qualified(ip);
	double dbl = 1;
	destroy(&dbl);
	return bad;
}
