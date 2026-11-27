// [namespace.udecl]/19: a member class a using-declaration introduced has the
// declaration's access when named in the class.
class T
{
  protected:
    class L
    {
      public:
        int v = 3;
    };
    struct P {
        int v;
    };
};

class F : private T
{
  public:
    using T::L;
    using T::P;
};

struct U {
    int a(F::L *x) { return x->v; }
    int b(F::P *p) { return p->v; }
};

int main()
{
    F::L l;
    F::P p{1};
    return U().a(&l) - U().b(&p) - 2;
}
