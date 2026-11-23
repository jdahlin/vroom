/* [temp.mem]/2: a closure type may have member templates (the function call operator template of a generic lambda), called, with explicit template arguments of a lambda with a template-head, and from a local class's enclosing function */
int main()
{
    auto add = [](auto a, auto b) { return a + b; };
    auto tmpl = []<class T>(T t) { return t + 1; };
    struct local {
        int run(int x) { return x; }
    };
    return add(1, 2) - 3 + add(1L, 'a') - 98 + tmpl(1) - 2 + tmpl.operator()<long>(2) - 3 + local().run(0);
}
