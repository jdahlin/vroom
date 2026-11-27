/* a conversion function template called on an aggregate prvalue */
extern "C" int printf(const char *, ...);

struct NoCtx {
};
struct Ctx {
    const void *fArg;
    operator NoCtx() { return NoCtx{}; }
    template <typename T> operator T *() { return (T *)fArg; }
};

struct pair {
    int a, b;
};

static int sum(const pair *p, const int *q) { return p->a + p->b * 10 + *q; }
static int none(NoCtx) { return 100; }

static void run(const void **program)
{
    const pair *p = Ctx{program[0]};
    NoCtx n = Ctx{program[1]};
    printf("%d %d %d\n", sum(Ctx{program[0]}, Ctx{program[1]}), p->b,
           none(Ctx{nullptr}) + none(n));
}

int main()
{
    pair p = {3, 4};
    int q = 500;
    const void *program[] = {&p, &q};
    run(program);
    int *direct = Ctx{&q};
    printf("%d\n", *direct);
    return 0;
}
