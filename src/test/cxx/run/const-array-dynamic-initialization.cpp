/* [basic.start.static]/2: a const array initialized dynamically is written at
   start-up: the const of an array is its elements' ([basic.type.qualifier]),
   so it must not go to read-only memory (NLOHMANN_JSON_SERIALIZE_ENUM's
   static const std::pair array) */
struct S {
    int a;
    int *p;
    S(int x) : a(x), p(&a)
    {
    }
    ~S()
    {
        a = 0;
    }
};
struct A {
    int a;
    int b;
};
static int next(int i)
{
    return i + 1;
}

const S global[] = {1, 2};
const S nested[][2] = {{3, 4}};

int main()
{
    static const S local[] = {5, 6};
    static const int scalars[] = {next(1), 7};
    static const A aggregates[] = {{next(2), 8}};
    if (global[1].a != 2 || *global[0].p != 1 || nested[0][1].a != 4)
        return 1;
    if (local[0].a != 5 || *local[1].p != 6)
        return 2;
    if (scalars[0] != 2 || scalars[1] != 7)
        return 3;
    if (aggregates[0].a != 3 || aggregates[0].b != 8)
        return 4;
    return 0;
}
