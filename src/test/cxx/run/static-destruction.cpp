/* [basic.start.term]/1, /3: every object of static storage duration with
   a destructor is destroyed at exit, in the reverse order of the
   completion of its constructor: one with no initializer (zero-initialized,
   a trivial default constructor), arrays (their elements last first, one
   of two dimensions too), a block-scope static under its guard whose
   initializer has temporaries of its own ([intro.execution]/5), and a
   block-scope static array. */
extern "C" int printf(const char *, ...);

struct T {
    int v;
    ~T() { printf("~T(%d)\n", v); }
};
struct U {
    int v;
    U(int v) : v(v) { printf("U(%d)\n", v); }
    ~U() { printf("~U(%d)\n", v); }
    int get() const { return v; }
};

T plain;
T row[3] = {{1}, {2}, {3}};
T grid[2][2] = {{{10}, {11}}, {{12}, {13}}};
U made(U(20).get() + 1);

int local(int k)
{
    static U once(U(30 + k).get() * 2);
    static T tens[2] = {{40}, {41}};
    return once.v + tens[1].v;
}

int main()
{
    plain.v = 5;
    printf("main\n");
    int s = local(1) + local(2);
    printf("%d %d\n", s, made.v);
    return s - 2 * (62 + 41);
}
