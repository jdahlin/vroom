// [temp.variadic]/5: a pack expansion in the parameter list of a function
// type is that type's, not the declarator's: the parameter need not be last.
struct C {
    int v;
    template <typename R, typename... Args> static int call(R proc(Args...), int ctx)
    {
        return proc(ctx);
    }
};

static int twice(int x) { return 2 * x; }

int main()
{
    return C::call(twice, 20) - 40;
}
