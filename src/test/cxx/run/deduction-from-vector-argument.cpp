/* a vector argument is no class: a template whose parameter is a
   template-id is no candidate, and the others are */
extern "C" int printf(const char *, ...);

typedef float F __attribute__((ext_vector_type(4)));
typedef int G __attribute__((vector_size(16)));

template <class T> struct box {
};

template <class T> static int pick(box<T> &, box<T> &) { return 1; }
template <class T> static int pick(T &a, T &b)
{
    T t = a;
    a = b;
    b = t;
    return 2;
}

int main()
{
    F a = {1, 2, 3, 4}, b = {5, 6, 7, 8};
    G c = {1, 2, 3, 4}, d = {5, 6, 7, 8};
    box<int> x, y;
    int p = pick(a, b), q = pick(c, d), r = pick(x, y);
    printf("%d %d %d %g %g %d %d\n", p, q, r, a[0], b[3], c[0], d[3]);
    return 0;
}
