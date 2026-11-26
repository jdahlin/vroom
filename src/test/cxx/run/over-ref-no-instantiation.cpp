/* [temp.inst]/9: a candidate whose parameter is a reference to a class
   template specialization unrelated to the argument is not viable without
   instantiating it (json's output_adapter<uint8_t> and its
   basic_ostream<uint8_t> & constructor) */
template <class T> struct Stream {
    typename T::type x;
};
struct Vector {
    int n;
};
struct Adapter {
    int which;
    Adapter(Stream<int> &) : which(2)
    {
    }
    Adapter(Vector &v) : which(v.n)
    {
    }
};
static int f(Stream<long> &)
{
    return 2;
}
static int f(Vector &v)
{
    return v.n;
}
static int g(Adapter a)
{
    return a.which;
}

int main()
{
    Vector v{1};
    if (f(v) != 1)
        return 1;
    if (g(v) != 1)
        return 2;
    return 0;
}
