// RUN: %cxx -std=c++20 %s -o %t && %t
// C++20 parenthesized aggregate initialization ([dcl.init]/17.6.2.2, P0960R3).

inline void *operator new(__SIZE_TYPE__, void *p) noexcept { return p; }

struct Inner {
    int x;
    int y;
};

struct Wrapper {
    Inner inner;
    int sum() const { return inner.x + inner.y; }
};

struct WithDefault {
    int a;
    int b = 7;
    int c;
};

template <class T, class... Args>
T *my_construct_at(T *loc, Args &&...args) {
    return ::new (static_cast<void *>(loc)) T(static_cast<Args &&>(args)...);
}

int main() {
    // 1. Placement new via template pack expansion (std::construct_at pattern)
    alignas(Wrapper) unsigned char wbuf[sizeof(Wrapper)];
    Inner in{10, 32};
    Wrapper *w = my_construct_at(reinterpret_cast<Wrapper *>(wbuf), in);
    if (w->sum() != 42)
        return 1;

    // 2. Multi-argument parenthesized aggregate init via placement new
    alignas(WithDefault) unsigned char dbuf[sizeof(WithDefault)];
    WithDefault *d1 = my_construct_at(reinterpret_cast<WithDefault *>(dbuf), 3, 4, 5);
    if (d1->a != 3 || d1->b != 4 || d1->c != 5)
        return 2;

    // 3. Omitted trailing members (default member initializer + value-init)
    alignas(WithDefault) unsigned char dbuf2[sizeof(WithDefault)];
    WithDefault *d2 = my_construct_at(reinterpret_cast<WithDefault *>(dbuf2), 11);
    if (d2->a != 11 || d2->b != 7 || d2->c != 0)
        return 3;

    // 4. Direct variable initialization with narrowing conversion allowed
    WithDefault d3(2.9, 8.7);
    if (d3.a != 2 || d3.b != 8 || d3.c != 0)
        return 4;

    // 5. Copy construction still preferred when argument is same aggregate type
    Wrapper wcopy(*w);
    if (wcopy.sum() != 42)
        return 5;

    return 0;
}
