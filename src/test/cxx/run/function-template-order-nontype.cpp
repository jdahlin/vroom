/* [temp.func.order], [temp.deduct.partial]/12: a non-type template
   parameter that is not used in the types being ordered takes no part in the
   partial ordering of two function templates (libc++'s __math::fmax). */
template <class = int>
int pick(double, double)
{
    return 1;
}

template <class A, class B, int N = 0>
int pick(A, B)
{
    return 2;
}

template <class T, int N = 3>
int one(T *)
{
    return 3;
}

template <class T, int N = 4>
int one(T)
{
    return 4;
}

int main()
{
    if (pick(1.0, 2.0) != 1)
        return 1;
    if (pick(1, 2.0) != 2)
        return 2;
    int x = 0;
    if (one(&x) != 3)
        return 3;
    if (one(x) != 4)
        return 4;
    return 0;
}
