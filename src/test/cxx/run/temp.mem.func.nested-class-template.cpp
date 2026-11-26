// [temp.mem.func]: out-of-line member function definition of a class template
// nested inside a non-template class (including partial specializations, e.g.
// `ScriptObject::ScriptDoCommandHelper<Tcmd, Tret(*)(...)>::Execute`).

struct Outer {
    template <class T> struct Helper;
    template <class R, class A> struct Helper<R (*)(A)> {
        static R run(A a);
    };
};

template <class R, class A> R Outer::Helper<R (*)(A)>::run(A a)
{
    return static_cast<R>(a + 1);
}

int main()
{
    if (Outer::Helper<int (*)(int)>::run(41) != 42)
        return 1;
    return 0;
}
