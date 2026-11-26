// [dcl.spec.auto]: calling a member function with a postponed `auto` return
// type inherited from a CRTP base class template (e.g. `d.begin()`).

template <class T> struct Box {
    T v;
};

template <class D, class T> struct Base {
    auto get() const { return Box<T>{42}; }
};

template <class T> struct Derived : Base<Derived<T>, T> {};

int main()
{
    Derived<int> d{};
    auto b = d.get();
    if (b.v != 42)
        return 1;
    return 0;
}
