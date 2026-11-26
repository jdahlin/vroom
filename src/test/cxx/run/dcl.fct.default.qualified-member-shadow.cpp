// [dcl.fct.default]: a qualified name in a default argument whose component
// matches a non-static data member or parameter name is not a use of that
// member or parameter (e.g. `std::source_location::current()` when the class
// has a member named `source_location`).

namespace ns {
struct loc {
    int line;
    static constexpr loc current() { return loc{42}; }
};
} // namespace ns

struct Window {
    Window(ns::loc location = ns::loc::current()) : loc(location) {}
    ns::loc loc;
};

int main()
{
    Window w;
    if (w.loc.line != 42)
        return 1;
    return 0;
}
