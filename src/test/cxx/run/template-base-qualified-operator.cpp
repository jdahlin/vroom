// Qualified call to an overloaded operator of a class template base
// (TArray<T, M>::operator=(that)), where '::' follows the template-id.

template <typename T, bool M>
struct Base {
    T value;
    Base& operator=(const Base& other) {
        value = other.value;
        return *this;
    }
};

template <typename T, bool M = true>
struct Derived : Base<T, M> {
    Derived& operator=(const Derived& other) {
        Base<T, M>::operator=(other);
        return *this;
    }
};

int main() {
    Derived<int> a;
    a.value = 1;
    Derived<int> b;
    b.value = 99;
    a = b;
    return a.value == 99 ? 0 : 1;
}
