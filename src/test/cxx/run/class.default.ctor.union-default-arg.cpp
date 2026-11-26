// [class.default.ctor]/1,/2: a union with a user-provided default constructor
// (including one with default arguments like `union arg_ref { arg_ref(int = 0); }`)
// does not have a deleted defaulted default constructor even when another
// variant member has a non-trivial default constructor.

struct View {
    const char *p;
    constexpr View() : p(nullptr) {}
};

union ArgRef {
    constexpr ArgRef(int i = 0) : index(i) {}
    constexpr ArgRef(View v) : name(v) {}
    int index;
    View name;
};

struct Specs {
    ArgRef width_ref;
    ArgRef precision_ref;
};

static_assert(__is_constructible(ArgRef));
static_assert(__is_constructible(Specs));

int main()
{
    Specs s;
    if (s.width_ref.index != 0 || s.precision_ref.index != 0)
        return 1;
    ArgRef a(5);
    if (a.index != 5)
        return 2;
    return 0;
}
