// [expr.cond]/4: operands of different types, one a class: the conversion sequence of each to the
// other's type; value category and type of the result
template <class T, class U> struct same { static constexpr bool value = false; };
template <class T> struct same<T, T> { static constexpr bool value = true; };

static int copies;
struct A {
	int v;
	A(int x) : v(x) {}
	A(const A &o) : v(o.v) { copies++; }
};
struct Base { int b; Base(int x) : b(x) {} };
struct Der : Base { Der(int x) : Base(x) {} };
struct ToInt { int v; operator int() const { return v; } };
struct Plain { int v; };

bool yes = true, no = false;
A a(7);
const A ca(8);
Base base(1);
Der der(2);
const Der cder(3);
int i = 5;
Plain plain = {4};
ToInt ti = {6};
Base *bp = &base;
Der *dp = &der;
const Base *cbp = &base;

// 4.3.3: a prvalue operand converted to the class by a converting constructor
static_assert(same<decltype(yes ? a : 1), A>::value, "class lvalue vs int");
static_assert(same<decltype(yes ? 1 : a), A>::value, "int vs class lvalue");
static_assert(same<decltype(yes ? A(1) : 2), A>::value, "class prvalue vs int");
// 4.1: a derived class lvalue binds directly to the base class lvalue
static_assert(same<decltype(yes ? der : base), Base &>::value, "derived lvalue vs base lvalue");
static_assert(same<decltype(no ? base : der), Base &>::value, "base lvalue vs derived lvalue");
static_assert(same<decltype(yes ? der : cder), const Der &>::value, "cv only");
static_assert(same<decltype(yes ? base : base), Base &>::value, "same lvalue");
// a conversion function: the class operand converts to the other's type
static_assert(same<decltype(yes ? ti : 1), int>::value, "conversion function");
static_assert(same<decltype(yes ? 1.5 : ti), double>::value, "conversion function and double");
// composite pointer types of pointers to related classes
static_assert(same<decltype(yes ? bp : dp), Base *>::value, "pointer to base vs derived");
static_assert(same<decltype(yes ? dp : cbp), const Base *>::value, "cv-combined, base");

int pick_a(bool c) { return (c ? a : 9).v; }
int pick_ca(bool c) { return (c ? ca : 9).v; }
int pick_der(bool c) { return (c ? der : base).b; }
int pick_der_ref(bool c)
{
	(c ? der : base).b = 40;
	return base.b;
}
int pick_conv(bool c) { return c ? ti : 3; }
int pick_ptr(bool c) { return (c ? bp : dp)->b; }

int main()
{
	if (pick_a(true) != 7 || pick_a(false) != 9)
		return 1;
	// the lvalue operand is copied (a prvalue result): once, only when chosen
	copies = 0;
	pick_a(true);
	pick_a(false);
	if (copies != 1)
		return 2;
	if (pick_ca(true) != 8 || pick_ca(false) != 9)
		return 3;
	if (pick_der(true) != 2 || pick_der(false) != 1)
		return 4;
	if (pick_ptr(true) != 1 || pick_ptr(false) != 2)
		return 8;
	// derived vs base lvalues: the result is an lvalue of the base class
	if (pick_der_ref(true) != 1 || der.b != 40)
		return 5;
	if (pick_der_ref(false) != 40)
		return 6;
	if (pick_conv(true) != 6 || pick_conv(false) != 3)
		return 7;
	(void)plain;
	(void)i;
	return 0;
}
