/* [temp.dep.candidate]/1: a function declared after the template in an
   associated namespace of a dependent call's argument is found at
   instantiation; an early overload beside it that is not viable, or one it
   is not better than, is unaffected (OpenTTD's GetCargoSuffix). */
struct S {
    int v;
};

static int suffix(S s, int a) { return s.v + a; }

template <class T> int all(const T &s) { return suffix(s, 2); }

/* only the late one takes two ints and an S: dependent, so it is found */
template <class T> int late_only(const T &s) { return suffix(s, 1, 2); }

static int suffix(S s, int a, int b) { return s.v * 100 + a * 10 + b; }

int main()
{
    if (all(S{5}) != 7)
        return 1;
    if (late_only(S{3}) != 312)
        return 2;
    return 0;
}
