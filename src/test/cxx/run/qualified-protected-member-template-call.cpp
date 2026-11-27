// [class.access.base]/5: this->B::make<W>() names a protected member template
// of the base from the derived class.
struct B {
  protected:
    template <class W> W make(int o) const { return W(o + v); }
    template <class W> static W zero() { return W(0); }
    int v = 1;
};
struct D : private B {
    D(int x) : n(x) {}
    D make2(int o) const { return this->B::make<D>(o); }
    D make3() const { return this->B::zero<D>(); }
    int n;
};

int main()
{
    D d(0);
    return d.make2(2).n + d.make3().n - 3;
}
