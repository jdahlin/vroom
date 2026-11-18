// [stmt.dcl]/4: a block-scope variable with static storage duration is
// initialized the first time control passes through its declaration, once;
// [basic.start.term]/3: destroyed after main, in the reverse order of
// construction; [abi:guards]: a guard variable _ZGV..., __cxa_guard_acquire
// and __cxa_guard_release around the initialization
#include <stdio.h>

static int calls;
int next() { return ++calls; }

struct Counted {
    int value;
    Counted(int v) : value(v) { printf("construct %d\n", value); }
    ~Counted() { printf("destroy %d\n", value); }
};

struct Constant {
    int value;
    ~Constant() { printf("destroy constant %d\n", value); }
};

int once()
{
    static int n = next();
    return n;
}

int counted(int v)
{
    static Counted c(v);
    return c.value;
}

int constant()
{
    static Constant k = {7};
    return k.value++;
}

int &reference()
{
    static int storage = 40;
    static int &r = (next(), storage);
    return r;
}

int main()
{
    int a = once(), b = once(), c = once();
    printf("once %d %d %d calls %d\n", a, b, c, calls);
    printf("counted %d %d\n", counted(10), counted(20));
    printf("constant %d %d\n", constant(), constant());
    reference() += 2;
    printf("reference %d calls %d\n", reference(), calls);
    for (int i = 0; i < 3; i++)
        printf("loop %d\n", counted(30 + i));
    return 0;
}
