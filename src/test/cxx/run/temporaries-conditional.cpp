/* [class.temporary]/4 with [expr.cond], [expr.log.and], [expr.log.or]: a
   temporary created in an operand that may not be evaluated is destroyed
   at the end of the full-expression only if it was created (clang keeps a
   flag per such temporary).  Temporaries with destructors, arguments and
   results passed by address, under ?:, && and ||, nested, and one bound to
   a reference ([class.temporary]/6). */
extern "C" int printf(const char *, ...);

struct T {
    int v;
    T(int v) : v(v) { printf("T(%d)\n", v); }
    T(const T &o) : v(o.v) { printf("T copy(%d)\n", v); }
    ~T() { printf("~T(%d)\n", v); }
    int get() const { return v; }
};

int value(T t) { return t.v; }
T make(int v) { return T(v); }

int pick(int c)
{
    return c ? T(1).get() : make(2).get();
}

int main()
{
    int s = 0;
    for (int c = 0; c < 2; c++) {
        printf("-- %d\n", c);
        s += pick(c);
        s += c && value(T(3)) == 3;
        s += c || make(4).get() == 4;
        s += c ? (c > 5 ? T(5).get() : value(T(6))) : 0;
        printf("end\n");
    }
    for (int c = 0; c < 2; c++) {
        const T &r = c ? T(7) : make(8);
        printf("bound %d\n", r.get());
    }
    return s - 8;
}
