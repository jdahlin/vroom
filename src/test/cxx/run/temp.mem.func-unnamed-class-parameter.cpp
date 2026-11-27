/* [temp.mem.func]: a member definition names a template parameter the
   class's declaration left unnamed. */
extern "C" int printf(const char *, ...);

template <typename Base, int> class Buffers final : public Base
{
  public:
    static int make(int);
    int get() const;
    int own = 1;
};

template <typename Base, int Type> int Buffers<Base, Type>::make(int v)
{
    return v + Type;
}
template <typename Base, int Type> int Buffers<Base, Type>::get() const
{
    return Type * 10 + this->own + this->base;
}

struct B {
    int base = 2;
};

int main()
{
    Buffers<B, 3> b;
    printf("%d %d\n", Buffers<B, 3>::make(4), b.get());
    return 0;
}
