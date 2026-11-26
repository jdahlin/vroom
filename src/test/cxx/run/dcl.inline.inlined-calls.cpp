/* Inline functions, members and template specializations may be inlined:
   destructors, static locals and early returns keep their meaning
   ([dcl.inline], [stmt.dcl]). */
int destroyed;

struct D {
    int v;
    ~D() { destroyed += v; }
};

inline int with_destructor(int v)
{
    D d{v};
    if (v > 2)
        return d.v * 10;
    return d.v;
}

inline int counter()
{
    static int n;
    return ++n;
}

template <class T> T twice(T v) { return v * 2; }

struct M {
    int v;
    int get() const { return v; }
    inline __attribute__((noinline)) int kept() const { return v + 1; }
};

inline int recurse(int n) { return n ? n + recurse(n - 1) : 0; }

inline const int &pick(const int &a, const int &b) { return b < a ? b : a; }

int main()
{
    if (with_destructor(1) + with_destructor(3) != 31 || destroyed != 4)
        return 1;
    if (counter() != 1 || counter() != 2)
        return 4;
    M m{4};
    if (twice(m.get()) != 8 || m.kept() != 5)
        return 5;
    if (recurse(10) != 55)
        return 6;
    int a = 3, b = 2;
    if (pick(a, b) != 2 || &pick(a, b) != &b)
        return 7;
    return 0;
}
