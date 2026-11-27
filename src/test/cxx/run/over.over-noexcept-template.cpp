/* [over.over]/2 with [conv.fctptr]: a noexcept function template's
   specialization is selected for a target that may throw */
template <class T> T twice(T x) noexcept { return x + x; }
int twice(int x) noexcept { return 3 * x; }
float apply(float (*f)(float)) { return f(2); }
int main()
{
    float (*p)(float) = twice;
    float (&r)(float) = twice;
    int (*q)(int) = &twice;
    if (p(1) != 2 || r(2) != 4 || q(2) != 6 || apply(twice) != 4)
        return 1;
    return 0;
}
