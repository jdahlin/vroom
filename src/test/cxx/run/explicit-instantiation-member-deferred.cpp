// Explicit instantiation declarations and definitions of out-of-class member
// functions of class templates must not loop when unneeded bodies are deferred.

template <class T>
struct Box {
    T val;
    T get(int add) const;
    T get(double) const;
};

template <class T>
T Box<T>::get(int add) const
{
    return val + add;
}

template <class T>
T Box<T>::get(double) const
{
    return T::nonexistent();
}

// Explicit instantiation declaration: must not instantiate or loop on deferred
// out-of-class member definitions.
extern template int Box<int>::get(int) const;

// Explicit instantiation definition of one overload: must define get(int)
// without instantiating the unneeded get(double) overload.
template int Box<int>::get(int) const;

// Also test a second specialization with only an extern template declaration
// followed by a normal call.
extern template long Box<long>::get(int) const;
template long Box<long>::get(int) const;

int main()
{
    Box<int> a{40};
    if (a.get(2) != 42)
        return 1;
    Box<long> b{100};
    if (b.get(23) != 123)
        return 2;
    return 0;
}
