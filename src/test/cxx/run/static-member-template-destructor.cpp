// [class.dtor]/15, [basic.start.term]/1: a static data member of a class
// template's specialization, or an inline one of a class, has static
// storage duration and is destroyed at the program's end.  Its destructor,
// an instantiated member's, is needed though nothing else uses it
// ([temp.inst]/4), also when the member's class takes the class being
// defined as its argument.
#include <stdio.h>

template <class T> struct Default {
    ~Default() { puts("destroy default"); }
};

template <class T> struct Noisy {
    T *p = nullptr;
    int id;
    Noisy(int i) : id(i) {}
    ~Noisy() { printf("destroy %d %d\n", id, (int)(p != nullptr)); }
};

template <class T> struct Holder {
    static inline Noisy<T> plain{1};
    static inline Default<Holder> self;
};
template struct Holder<int>;

template <class T> struct Uninitialized {
    static inline Default<Uninitialized> self;
};
template struct Uninitialized<char>;

struct Plain {
    static inline Noisy<int> self{4};
};

// (a variable of this unit's own, for the unit's initialization function)
Noisy<char> global(5);

int main()
{
    printf("%d %d\n", Holder<int>::plain.id,
           Plain::self.id);
    return 0;
}
