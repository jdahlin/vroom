/* [stmt.pre]/4 and [stmt.select], [stmt.iter]: a variable declared in a
   condition or an init-statement, constructed by a constructor and with a
   destructor: [stmt.if]/3, [stmt.switch]/7 and [stmt.for]/1 put the
   init-statement before the statement in a block; [stmt.while]/2 makes the
   condition's variable live for one iteration, destroyed before the next
   test, also on break and continue ([stmt.jump]/2). */
extern "C" int printf(const char *, ...);

struct T {
    int v;
    T(int v) : v(v) { printf("T(%d)\n", v); }
    ~T() { printf("~T(%d)\n", v); }
    explicit operator bool() const { return v < 4; }
};

int main()
{
    if (T a(1); a.v == 1)
        printf("if %d\n", a.v);
    if (T b = T(2))
        printf("cond %d\n", b.v);
    else
        printf("else %d\n", b.v);
    switch (T s(3); s.v) {
    case 3:
        printf("switch %d\n", s.v);
        break;
    }
    int i = 0;
    while (T w = T(i)) {
        i++;
        if (i == 2)
            continue;
        if (i == 3)
            break;
        printf("while %d\n", w.v);
    }
    for (T f(10); f.v < 13; f.v++) {
        if (f.v == 11)
            continue;
        printf("for %d\n", f.v);
    }
    for (int j = 0; T g = T(j); j++) {
        if (j == 3)
            break;
        printf("for-cond %d\n", g.v);
    }
    return 0;
}
