/* [dcl.mptr], [expr.unary.op]/3, /4, [expr.mptr.oper], [conv.mem],
   [expr.eq]/4, [conv.bool], [expr.static.cast]/12, [conv.qual]: pointers
   to data members and to member functions, virtual ones too, through
   bases at nonzero offsets, compared, tested, passed, returned and stored;
   qualification conversions; .* on a prvalue ([basic.lval]/7) */
struct Pad {
    long p;
    virtual int first() { return 1; }
};
struct A {
    int x;
    int y;
    int get() { return x; }
    int add(int k) { return x + k; }
    virtual int v() { return 10 + y; }
    virtual int w() { return 20 + y; }
};
struct B : Pad, A {
    int z;
    int v() { return 30 + z; }
    int own() { return z * 2; }
};
struct C {
    int c;
};
struct D : B, C {
    int d;
};

int A::*global_data = &A::y;
int A::*global_null = 0;
int (A::*global_function)() = &A::get;
int (A::*global_virtual)() = &A::w;
int (A::*global_function_null)() = 0;
int B::*global_converted = &A::x;

int read(A *a, int A::*p) { return a->*p; }
int call(A &a, int (A::*f)()) { return (a.*f)(); }
int B::*widen(int A::*p) { return p; }
int (B::*widen_function(int (A::*f)()))() { return f; }
bool test(int A::*p) { return p; }
bool test_function(int (A::*f)()) { return f; }
bool same(int (A::*f)(), int (A::*g)()) { return f == g; }

const int A::*global_const = &A::y;
const int B::*global_const_converted = &A::x;
int rank(const int A::*) { return 1; }
int rank(const volatile int A::*) { return 2; }
struct E {
    int x;
    int y;
    E(int k) : x(k), y(k + 1) {}
    int get() { return x; }
    int sum(int k) { return x + y + k; }
};
E make(int x) { return E(x); }

static void fail() { __builtin_abort(); }

int main()
{
    B b;
    b.p = 1;
    b.x = 2;
    b.y = 3;
    b.z = 4;
    A &a = b;
    /* data members */
    int A::*px = &A::x;
    if (b.*px != 2 || a.*px != 2 || (&b)->*px != 2)
        fail();
    if (read(&a, global_data) != 3)
        fail();
    a.*px = 7;
    if (b.x != 7)
        fail();
    int B::*pz = &B::z;
    if (b.*pz != 4)
        fail();
    /* [conv.mem]/2: base to derived, at the base's offset */
    int B::*py = px;
    py = global_data;
    if (b.*py != 3 || b.*global_converted != 7 || b.*widen(px) != 7)
        fail();
    /* [expr.static.cast]/12: back to the base */
    int A::*back = static_cast<int A::*>(pz);
    if (static_cast<int A::*>(py) != global_data || back == px)
        fail();
    /* null member pointers ([conv.mem]/1), tested and compared */
    int A::*none = 0;
    int B::*none_derived = none;
    if (none || test(global_null) || !test(px) || none_derived ||
        none != global_null || px == global_data || !(px != global_data))
        fail();
    if (static_cast<int A::*>(none_derived) != 0 || none_derived != nullptr)
        fail();
    D d;
    d.x = 11;
    int D::*dx = &A::x;
    if (d.*dx != 11 || (&d)->*dx != 11)
        fail();
    /* member functions */
    int (A::*get)() = &A::get;
    if ((a.*get)() != 7 || call(b, global_function) != 7)
        fail();
    int (A::*add)(int) = &A::add;
    if ((a.*add)(5) != 12 || ((&b)->*add)(1) != 8)
        fail();
    /* a virtual function: the final overrider of the object */
    int (A::*v)() = &A::v;
    if ((a.*v)() != 34 || call(b, global_virtual) != 23)
        fail();
    A plain;
    plain.x = 0;
    plain.y = 1;
    if ((plain.*v)() != 11 || call(plain, &A::w) != 21)
        fail();
    /* through a base at a nonzero offset */
    int (B::*own)() = &B::own;
    int (B::*bget)() = get;
    int (B::*bv)() = widen_function(v);
    if ((b.*own)() != 8 || (b.*bget)() != 7 || (b.*bv)() != 34)
        fail();
    int (D::*dv)() = &A::v;
    d.z = 5;
    if ((d.*dv)() != 35)
        fail();
    if (static_cast<int (A::*)()>(bget) != get)
        fail();
    /* tests and comparisons of member function pointers */
    int (A::*nothing)() = 0;
    if (nothing || test_function(global_function_null) || !test_function(v) ||
        !get || same(get, v) || !same(v, &A::v) || !same(nothing, 0) ||
        get == nothing || v != &A::v)
        fail();
    get = 0;
    if (get != nullptr)
        fail();
    get = &A::get;
    if (!get)
        fail();
    /* [conv.qual]: a pointer to member of a more qualified type, with
       [conv.mem]/2 too */
    int A::*ay = &A::y;
    const int A::*cy = ay;
    const volatile int A::*cvy = cy;
    const int B::*cby = ay;
    if (b.*cy != 3 || b.*cvy != 3 || b.*cby != 3 || a.*global_const != 3 ||
        b.*global_const_converted != 7)
        fail();
    if (cy != ay || ay != cy || !(cby == ay) || cy == &A::x ||
        static_cast<const int A::*>(ay) != cy || rank(ay) != 1)
        fail();
    const int B::*bcy = static_cast<const int B::*>(ay);
    const int A::*acy = static_cast<const int A::*>(&B::y);
    if (b.*bcy != 3 || a.*acy != 3 || (const int B::*)ay != bcy)
        fail();
    /* .* on a prvalue: its temporary, an xvalue */
    int E::*ex = &E::x;
    int E::*ey = &E::y;
    int (E::*eget)() = &E::get;
    int (E::*esum)(int) = &E::sum;
    int &&r = make(40).*ex;
    const int E::*cey = ey;
    if (r != 40 || make(5).*ey != 6 || make(5).*cey != 6 ||
        (make(6).*eget)() != 6 || (make(7).*esum)(1) != 16 ||
        (E(8).*eget)() != 8)
        fail();
    return 0;
}
