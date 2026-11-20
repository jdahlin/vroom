/* [basic.lookup.classref]/1, [temp.names]/3: after . or -> a name followed
   by < is a template-name only if looked up in the class of the object
   expression it finds a template (or, not found there, the context finds a
   class template); else < is the less-than operator.  The parser has no
   types, so what a class declares as a member template decides:
   x86-64's fp.cpp, l->offset < INT32_MIN || l->offset > INT32_MAX, where
   the macro's value is parenthesized: > (...) looks like a call. */
#define LIMIT (100)
template <class T> struct ct {
    static int tag(int x) { return x + 1; }
};
template <class T> int ft(T x) { return x; }
struct S {
    int offset;
    long big;
    int f(int x) { return x; }
    template <class T> int g(int x) { return x + sizeof(T); }
};
template <class T> struct Box {
    T value;
    int count;
    template <class U> int convert(int x) { return x + sizeof(U); }
};

int object(S &r)
{
    int a = r.offset < 5 || r.offset > LIMIT;
    int b = r.offset < r.big && r.big > (3);
    int c = r.g<char>(1) + r.template g<int>(2);
    int d = r.f(1) < 3 || r.f(2) > (3);
    return a + b * 2 + c * 4 + d * 100;
}
int pointer(S *p)
{
    int a = p->offset < 5 || p->offset > LIMIT;
    int c = p->g<char>(1) + p->template g<int>(2);
    return a + c * 4;
}
template <class U> int dependent(U &u, S &s)
{
    int a = s.offset < 5 || s.offset > LIMIT;
    int b = u.offset < 5 || u.offset > LIMIT;
    int c = u.template g<int>(1) + s.g<int>(2);
    return a + b * 2 + c * 4;
}
template <class T> int of_template(Box<T> &box)
{
    int a = box.count < 5 || box.count > LIMIT;
    int b = box.value < 5 || box.value > (3);
    int c = box.template convert<char>(1) + box.template convert<int>(2);
    return a + b * 2 + c * 4;
}
int shadowed(S &r)
{
    /* a function template of the name in the context is no template-name
       after . ([basic.lookup.classref]/1 names a class template only) */
    int ft = 3;
    return r.offset < ft || r.offset > (7);
}
int main()
{
    S s{7, 9};
    Box<int> box{4, 6};
    int sum = object(s) + pointer(&s);
    sum += dependent(s, s) + of_template(box) + shadowed(s);
    return sum & 0x7f;
}
