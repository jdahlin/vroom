/* [class.static.data]/4, [dcl.array]/7: a constexpr static data member
   array initialized by a string literal, its bound from the literal */
extern "C" int strcmp(const char *, const char *);

struct C {
    static constexpr char name[] = "skia/x";
    inline static constexpr const char padded[8] = "ab";
};
static_assert(sizeof(C::name) == 7);

int main()
{
    const char *n = C::name;
    const char *volatile m = C::padded;
    if (strcmp(n, "skia/x") || C::name[3] != 'a')
        return 1;
    if (strcmp(m, "ab") || C::padded[5] != 0)
        return 2;
    return 0;
}
