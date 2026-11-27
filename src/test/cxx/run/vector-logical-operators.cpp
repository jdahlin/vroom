/* [ext:gnu_vector_size] &&, || and ! of vectors: a mask lane by lane, both
   operands evaluated */
extern "C" int printf(const char *, ...);

typedef float F __attribute__((ext_vector_type(4)));
typedef int I __attribute__((ext_vector_type(4)));

static int calls;
static I count(I v)
{
    calls++;
    return v;
}

static void show(const char *name, I v)
{
    printf("%s %d %d %d %d\n", name, v[0], v[1], v[2], v[3]);
}

int main(int argc, char **)
{
    F mn = {-1.0f, 1.0f, -2.0f, 2.0f}, l = {0.0f, 1.0f, -2.0f, 5.0f};
    show("and", mn < 0 && l != mn);
    show("or", mn < 0 || l != mn);
    I a = {0, 2, 0, 4 * argc}, b = {0, 0, 1, 8};
    show("a&&b", a && b);
    show("a||b", a || b);
    I zero = {};
    show("both", count(zero) && count(a));
    show("f", mn && l);
    show("!a", !a);
    show("!f", !l);
    show("!!", !count(b));
    printf("%d\n", calls);
    return 0;
}
