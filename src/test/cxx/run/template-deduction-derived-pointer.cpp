// [temp.deduct.call]/4.3: deduce a class template's arguments from a
// pointer to a derived class, as in libc++'s atomic load overloads.
template <class T> struct Base { T value; };

struct Padding { int pad; };
template <class T, class Parent = Base<T>>
struct Derived : Padding, Parent {};
struct Indirect : Derived<int> {};

template <class T> T read(const Base<T>* p) { return p->value + 1; }
template <class T> T read(const volatile Base<T>* p) { return p->value + 2; }

struct Mixed : Base<int>, Base<long> {};
int choose(const void*) { return 1; }
template <class T> int choose(const Base<T>*) { return 2; }

int main() {
    Derived<int> direct;
    direct.pad = 9;
    direct.value = 40;
    const volatile Derived<int>* qualified = &direct;
    Indirect indirect;
    indirect.pad = 9;
    indirect.value = 70;
    Mixed mixed;
    return choose(&mixed) != 1 || read(&direct) != 41 || read(qualified) != 42 ||
           read(&indirect) != 71;
}
