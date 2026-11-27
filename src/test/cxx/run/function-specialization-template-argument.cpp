/* the address of a function template's specialization, selected by the
   parameter's type or named, as a template argument */
extern "C" int printf(const char *, ...);

template <typename T> static void add(T *d) { *d += 1; }
template <typename T> void twice(T *d) { *d *= 2; }

template <typename T, void (*Apply)(T *)> static void each(T *a, T *end)
{
    do {
        Apply(a);
    } while (++a != end);
}

int main()
{
    int i[2] = {1, 2};
    float x[2] = {3, 4};
    each<int, &add>(i, i + 2);
    each<float, &add>(x, x + 2);
    each<int, &twice>(i, i + 2);
    each<float, twice<float>>(x, x + 2);
    printf("%d %d %g %g\n", i[0], i[1], x[0], x[1]);
    return 0;
}
