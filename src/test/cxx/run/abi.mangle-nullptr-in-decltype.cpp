/* [abi:mangling.literal]: nullptr in a dependent decltype is LDnE */
struct arena {
    template <class C> auto make(C &&c) -> decltype(c(nullptr))
    {
        return c(nullptr);
    }
};

int main()
{
    arena a;
    if (a.make([](void *p) { return p == nullptr; }))
        return 0;
    return 1;
}
