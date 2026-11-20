/* [class.copy.elision]/3: a local returned by name is first tried as an
   rvalue; a class that has a conversion function and converting
   constructors still copies by its implicit copy/move constructor
   (identity beats any user-defined conversion, [over.ics.rank]/2.1). */
struct Handle {
    int v;
    Handle() = default;
    Handle(int x) : v(x) {}
    Handle(const char *p) : v(p[0]) {}
    operator int() const { return v; }
    operator const char *() const { return "q"; }
};
static Handle make(int x)
{
    Handle h(x);
    h.v += 1;
    return h;
}
static Handle pick(Handle a, Handle b, bool first)
{
    Handle r = a;
    if (!first)
        r = b;
    return r;
}
int main()
{
    Handle h = make(4);
    int v = h;
    Handle p = pick(Handle(1), Handle(2), false);
    if (v == 5 && int(p) == 2)
        return 0;
    return 1;
}
