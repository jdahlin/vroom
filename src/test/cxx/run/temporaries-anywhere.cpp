/* [intro.execution]/5, [class.temporary]/4: a full-expression's
   temporaries are destroyed at its end wherever the full-expression is: a
   condition, a for statement's condition and step, a do statement's
   condition, a switch's condition.  Temporaries with destructors and class
   arguments and results passed by address, each traced. */
extern "C" int printf(const char *, ...);

struct T {
    int v;
    T(int v) : v(v) { printf("T(%d)\n", v); }
    T(const T &o) : v(o.v) { printf("T copy(%d)\n", v); }
    ~T() { printf("~T(%d)\n", v); }
    int get() const { return v; }
};

/* passed and returned by address ([abi:calls]: a non-trivial destructor) */
int value(T t) { return t.v; }
T make(int v) { return T(v); }

int main()
{
    int n = 0;
    if (T(1).get() == 1)
        printf("if\n");
    while (value(T(n)) < 2) {
        printf("while %d\n", n);
        n++;
    }
    for (int i = 0; make(i).get() < 2; i += value(T(10 + i)) - 9 - i)
        printf("for %d\n", i);
    int k = 0;
    do
        printf("do %d\n", k++);
    while (T(k).get() < 2);
    switch (make(3).get()) {
    case 3:
        printf("case 3\n");
        break;
    }
    return T(4).get() + make(5).get() - 9;
}
