/* [class.temporary]/4: temporaries of constructor arguments in namespace
   scope initializers, constant and dynamic. */
extern "C" int printf(const char *, ...);
struct Mono {};
enum class Mask { none, five = 5 };
struct Var {
    int index, value;
    Var(Mono &&) : index(0), value(0) {}
    Var(Mask &&m) : index(1), value((int)m) {}
};
struct Cb { const char *name; int (*read)(const void *); Var bit; int id; };
static const Cb table[] = {
    {"a", [](const void *) { return 1; }, Mono{}, 1},
    {"b", [](const void *) { return 2; }, Mask::five, 2},
};
int main()
{
    for (const Cb &c : table)
        printf("%s %d %d %d %d\n", c.name, c.read(nullptr), c.bit.index, c.bit.value, c.id);
    return 0;
}
