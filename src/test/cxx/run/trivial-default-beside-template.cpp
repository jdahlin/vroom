/* [class.default.ctor]/3: whether the default constructor is trivial in a
   class that also has a constructor template: a non-template one that takes
   no arguments is the one selected ([over.match.best]/2.4) and is
   user-provided */
extern "C" int printf(const char *, ...);

struct Beside {
    Beside() {}
    template <class T> Beside(T) {}
};
struct Defaulted_arguments {
    Defaulted_arguments(int = 0) {}
    template <class T> Defaulted_arguments(T *) {}
};
struct Member {
    Beside b;
};
struct Base : Beside {};
struct Array {
    Beside b[2];
};
template <class T> struct Container {
    Container() noexcept {}
    template <class I> Container(I, I) {}
    T *p;
};
struct Holder {
    Container<int> c;
    int n;
};
struct Defaulted {
    Defaulted() = default;
    template <class T> Defaulted(T) {}
    int n;
};

template <class T> void show(const char *name)
{
    printf("%s %d %d %d\n", name, int(__is_trivially_constructible(T)),
           int(__is_constructible(T)), int(__is_nothrow_constructible(T)));
}

int main()
{
    show<Beside>("Beside");
    show<Defaulted_arguments>("Defaulted_arguments");
    show<Member>("Member");
    show<Base>("Base");
    show<Array>("Array");
    show<Beside[3]>("Beside[3]");
    show<Container<int>>("Container");
    show<Holder>("Holder");
    show<Defaulted>("Defaulted");
}
