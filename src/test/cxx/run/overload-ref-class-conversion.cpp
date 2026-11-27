// [over.match.ref]/1: a non-const lvalue reference to a class type cannot bind
// via a conversion function that does not yield an lvalue.  Overload resolution
// between f(Target &) and f(Target &&) must reject f(Target &) rather than
// leaving its viability unknown when Source has non-lvalue conversion functions.

struct Target {
    int v;
    Target(int x) : v(x) {}
};

struct SourceWithPrvalueConv {
    int v;
    operator Target() const { return Target{v + 1}; }
};

static int pick(Target &t) { return t.v + 10; }
static int pick(Target &&t) { return t.v + 20; }

int main()
{
    SourceWithPrvalueConv s{5};
    // Must pick pick(Target &&) via SourceWithPrvalueConv::operator Target():
    // 5 + 1 + 20 = 26
    if (pick(s) != 26)
        return 1;

    Target t{7};
    // Direct lvalue binding must still pick pick(Target &):
    // 7 + 10 = 17
    if (pick(t) != 17)
        return 2;

    return 0;
}
