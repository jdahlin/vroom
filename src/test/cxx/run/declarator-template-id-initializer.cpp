// [dcl.ambig.res]/1: T x(U<A>(a.b)) cannot be a function declaration, since
// a.b is no declarator: the parentheses are an initializer, also when the
// type of the functional cast is a simple-template-id (abseil's
// inlined_vector.h).
template <class T> struct Iterator {
    T *at;
    explicit Iterator(T *at) : at(at) {}
};
template <class T> using Alias = Iterator<T>;
template <class A, class I> struct Adapter {
    I it;
    Adapter(I it) : it(it) {}
};
struct View {
    int *data;
};

int plain(View v)
{
    Adapter<int, Iterator<int>> a(Iterator<int>(v.data));
    Adapter<int, Alias<int>> b(Alias<int>(v.data + 1));
    return *a.it.at + *b.it.at;
}

template <class T> int dependent(View v)
{
    Adapter<T, Alias<T>> a(Alias<T>(v.data));
    Adapter<T, Iterator<T>> b(Iterator<T>{v.data + 1});
    return *a.it.at + *b.it.at;
}

template <class A> struct Storage {
    int insert(View v);
};
template <class A> int Storage<A>::insert(View v)
{
    Adapter<A, Alias<A>> a(
        Alias<A>(v.data));
    return *a.it.at;
}

// still a function declaration: (v) is a parameter with redundant parentheses
Iterator<int> declared()
{
    Iterator<int> make(Alias<int>(v));
    return make(Iterator<int>(nullptr));
}
Iterator<int> make(Iterator<int> it) { return it; }

int main()
{
    int values[2] = {3, 4};
    View v{values};
    if (plain(v) != 7)
        return 1;
    if (dependent<int>(v) != 7)
        return 2;
    if (Storage<int>().insert(v) != 3)
        return 3;
    if (declared().at != nullptr)
        return 4;
    return 0;
}
