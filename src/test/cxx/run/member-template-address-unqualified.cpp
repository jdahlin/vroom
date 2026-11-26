/* a static member template named unqualified in its class, as a value
   (Highway's static_cast<RunFunc>(&CallClosure<Closure>)) */
typedef void (*F)(int *);
class T
{
  public:
    template <class C> void Set(const C &)
    {
        f = static_cast<F>(&Call<C>);
    }
    void Run(int *p) { f(p); }

  private:
    template <class C> static void Call(int *p) { *p = sizeof(C); }
    F f;
};

int main()
{
    T t;
    int v = 0;
    t.Set(1);
    t.Run(&v);
    return v != 4;
}
